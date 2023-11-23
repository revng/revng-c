//
// Copyright rev.ng Labs Srl. See LICENSE.md for details.
//

#include "llvm/Analysis/LazyValueInfo.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/InitializePasses.h"
#include "llvm/Passes/PassBuilder.h"

#include "revng/Lift/LoadBinaryPass.h"
#include "revng/Model/Architecture.h"
#include "revng/Model/IRHelpers.h"
#include "revng/Model/LoadModelPass.h"
#include "revng/Model/RawBinaryView.h"
#include "revng/Model/RawFunctionType.h"
#include "revng/Pipeline/Location.h"
#include "revng/Pipeline/RegisterLLVMPass.h"
#include "revng/Pipes/Ranks.h"
#include "revng/Support/FunctionTags.h"
#include "revng/Support/IRHelpers.h"
#include "revng/ValueMaterializer/DataFlowGraph.h"
#include "revng/ValueMaterializer/MemoryOracle.h"
#include "revng/ValueMaterializer/ValueMaterializer.h"

#include "revng-c/Pipes/Kinds.h"
#include "revng-c/Support/FunctionTags.h"
#include "revng-c/Support/IRHelpers.h"
#include "revng-c/TypeNames/ModelTypeNames.h"

#include "SimplifySwitchPass.h"

using pipeline::serializedLocation;
namespace ranks = revng::ranks;
using BiDFNode = BidirectionalNode<DataFlowNode>;

static Logger<> SwitchOptLogger("switch-opt");

namespace {
class DataMemoryOracle final : public MemoryOracle {
private:
  RawBinaryView &BinaryView;
  model::Architecture::Values Arch;
  bool IsLittleEndian = false;

public:
  DataMemoryOracle(RawBinaryView &BinaryView,
                   model::Architecture::Values Arch) :
    BinaryView(BinaryView), Arch(Arch) {}
  ~DataMemoryOracle() final = default;

  MaterializedValue load(uint64_t LoadAddress, unsigned LoadSize) final {
    auto Address = MetaAddress::fromGeneric(toLLVMArchitecture(Arch),
                                            LoadAddress);
    auto MaybeValue = BinaryView.readInteger(Address, LoadSize, IsLittleEndian);
    auto NewAPInt = [LoadSize](uint64_t V) {
      return llvm::APInt(LoadSize * 8, V);
    };
    if (MaybeValue)
      return MaterializedValue::fromConstant(NewAPInt(*MaybeValue));
    else
      return MaterializedValue::invalid();
  }
};
} // namespace

static llvm::Constant *toLLVMConstant(llvm::LLVMContext &Context,
                                      const llvm::APInt &Value) {
  using namespace llvm;
  llvm::Type *DestinationType = llvm::IntegerType::get(Context,
                                                       Value.getBitWidth());
  if (DestinationType->isPointerTy()) {
    revng_abort();
  } else if (auto *IT = dyn_cast<IntegerType>(DestinationType)) {
    revng_assert(IT->getBitWidth() == Value.getBitWidth());
    return ConstantInt::get(DestinationType, Value);
  } else {
    revng_abort();
  }
}

static llvm::BasicBlock *getBlockFor(llvm::SwitchInst *Switch,
                                     llvm::Constant *C) {
  uint64_t TheConstant = cast<llvm::ConstantInt>(C)->getValue().getZExtValue();
  for (const auto &I : Switch->cases()) {
    const llvm::ConstantInt *CaseVal = I.getCaseValue();
    uint64_t Case = CaseVal->getValue().getZExtValue();
    if (Case == TheConstant)
      return I.getCaseSuccessor();
  }

  return nullptr;
}

static bool runSimplifySwitch(llvm::Function &F,
                              llvm::LazyValueInfo &LVI,
                              llvm::DominatorTree &DT,
                              DataMemoryOracle &MO) {
  if (F.isDeclaration() || !F.hasExactDefinition())
    return false;

  for (auto &BB : F) {
    for (auto &I : BB) {
      auto Switch = dyn_cast<llvm::SwitchInst>(&I);
      if (not Switch)
        continue;

      auto *Condition = Switch->getCondition();
      DataFlowGraph::Limits Limits(1000 /*MaxPhiLike*/, 1 /*MaxLoad*/);
      auto Results = ValueMaterializer::getValuesFor(Switch,
                                                     Condition,
                                                     MO,
                                                     LVI,
                                                     DT,
                                                     Limits,
                                                     Oracle::AdvancedValueInfo);
      auto &DFG = Results.dataFlowGraph();
      // Identify the only node in the data flow graph that's associated to an
      // Oracle and use it as a switch condition.
      bool BailOut = false;
      BidirectionalNode<DataFlowNode> *StartNode = nullptr;

      for (auto BidirectNode : DFG.nodes()) {
        const DataFlowNode &Node = *BidirectNode;
        // Ignore nodes that are constants in the DFG.
        if (llvm::isa<llvm::ConstantInt>(Node.Value))
          continue;

        if (Node.UseOracle) {
          if (StartNode != nullptr) {
            BailOut = true;
            break;
          } else {
            // TODO: Fix this const_cast.
            auto BiNode = const_cast<BiDFNode *>(BidirectNode);
            StartNode = BiNode;
          }
        }
      }

      if (StartNode == nullptr or BailOut)
        continue;

      if (not Results.values())
        continue;

      auto StartNodeInst = dyn_cast<llvm::Instruction>(StartNode->Value);
      if (StartNodeInst and isa<llvm::PHINode>(StartNodeInst))
        continue;
      auto ConditionInst = dyn_cast<llvm::Instruction>(Condition);
      if (ConditionInst and isa<llvm::PHINode>(ConditionInst))
        continue;

      if (StartNode->Value == Condition)
        continue;

      if (Results.values()->size() != Switch->getNumCases()) {
        revng_log(SwitchOptLogger, "Unexpected values!");
        continue;
      }

      std::map<llvm::ConstantInt *, llvm::BasicBlock *> NewLabels;
      for (auto &Value : *StartNode->OracleRange) {
        auto Old = DFG.materializeOne(StartNode, MO, Value);
        if (not Old)
          return false;
        auto *ConstantForOld = toLLVMConstant(Switch->getContext(),
                                              (*Old).value());
        auto *ConstantForTheValue = toLLVMConstant(Switch->getContext(), Value);

        auto *BlockForValue = getBlockFor(Switch, ConstantForOld);

        if (not BlockForValue)
          return false;
        NewLabels[cast<llvm::ConstantInt>(ConstantForTheValue)] = BlockForValue;
      }

      // Recreate the new switch.
      llvm::IRBuilder<> Builder(Switch);
      auto *UnreachBB = llvm::BasicBlock::Create(Switch->getContext(),
                                                 "unreachable",
                                                 &F);
      llvm::IRBuilder<> IRBUnreachable(UnreachBB);
      IRBUnreachable.CreateUnreachable();
      auto *NewSwitch = Builder.CreateSwitch(StartNode->Value,
                                             UnreachBB,
                                             NewLabels.size());

      for (auto &NewLabel : NewLabels) {
        NewSwitch->addCase(NewLabel.first, NewLabel.second);
      }

      Switch->replaceAllUsesWith(NewSwitch);
      Switch->eraseFromParent();

      return true;
    }
  }

  return false;
}

SimplifySwitchPass::SimplifySwitchPass() : FunctionPass(ID) {
}

bool SimplifySwitchPass::runOnFunction(llvm::Function &F) {
  bool Changed = false;

  auto &DT = getAnalysis<llvm::DominatorTreeWrapperPass>().getDomTree();
  auto &LVI = getAnalysis<llvm::LazyValueInfoWrapperPass>().getLVI();

  RawBinaryView &BinaryView = getAnalysis<LoadBinaryWrapperPass>().get();
  const model::Binary
    &Model = *getAnalysis<LoadModelWrapperPass>().get().getReadOnlyModel();

  DataMemoryOracle MO(BinaryView, Model.Architecture());
  Changed |= runSimplifySwitch(F, LVI, DT, MO);

  return Changed;
}

void SimplifySwitchPass::getAnalysisUsage(llvm::AnalysisUsage &AU) const {
  AU.addRequired<llvm::DominatorTreeWrapperPass>();
  AU.addRequired<llvm::LazyValueInfoWrapperPass>();
  AU.addRequired<LoadBinaryWrapperPass>();
  AU.addRequired<LoadModelWrapperPass>();
}

char SimplifySwitchPass::ID = 0;
static constexpr const char *Flag = "simplify-switch";
using Register = llvm::RegisterPass<SimplifySwitchPass>;
static Register X(Flag, "Pass that simplifies switch statement.", false, false);
