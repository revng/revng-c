//
// Copyright (c) rev.ng Labs Srl. See LICENSE.md for details.
//

#include "revng/EarlyFunctionAnalysis/FunctionMetadata.h"
#include "revng/Model/Binary.h"
#include "revng/Model/IRHelpers.h"
#include "revng/Pipeline/Location.h"
#include "revng/Pipeline/RegisterPipe.h"
#include "revng/Pipes/IRHelpers.h"
#include "revng/Support/IRHelpers.h"
#include "revng/TupleTree/TupleTree.h"

#include "revng-c/Pipes/Kinds.h"
#include "revng-c/mlir/Dialect/Clift/IR/CliftOps.h"
#include "revng-c/mlir/Dialect/Clift/IR/ImportModel.h"
#include "revng-c/mlir/Pipes/MLIRContainer.h"

#include "MLIRLLVMHelpers.h"

using namespace revng::mlir_llvm;

using mlir::LLVM::LLVMFuncOp;
using LLVMCallOp = mlir::LLVM::CallOp;
using LLVMUndefOp = mlir::LLVM::UndefOp;

namespace {

template<typename Traits>
class BasicFunctionMetadataCache {
  using Function = typename Traits::Function;
  using CallInst = typename Traits::CallInst;

  std::map<typename Traits::Value, efa::FunctionMetadata> FunctionCache;

public:
  const efa::FunctionMetadata &getFunctionMetadata(Function Function) {
    auto Iterator = FunctionCache.find(Function);
    if (Iterator != FunctionCache.end())
      return Iterator->second;

    efa::FunctionMetadata FM = *Traits::extractFunctionMetadata(Function).get();
    return FunctionCache.try_emplace(Function, FM).first->second;
  }

  inline std::pair<std::optional<efa::CallEdge>, BasicBlockID>
  getCallEdge(const model::Binary &Binary, CallInst Call) {
    using namespace llvm;

    auto MaybeLocation = Traits::getLocation(Call);

    if (not MaybeLocation)
      return { std::nullopt, BasicBlockID::invalid() };

    auto BlockAddress = MaybeLocation->parent().back();

    Function ParentFunction = Traits::getParent(Traits::getParent(Call));
    const efa::FunctionMetadata &FM = getFunctionMetadata(ParentFunction);
    const efa::BasicBlock &Block = FM.ControlFlowGraph().at(BlockAddress);

    // Find the call edge
    efa::CallEdge *ModelCall = nullptr;
    for (auto &Edge : Block.Successors()) {
      if (auto *CE = dyn_cast<efa::CallEdge>(Edge.get())) {
        revng_assert(ModelCall == nullptr);
        ModelCall = CE;
      }
    }
    revng_assert(ModelCall != nullptr);

    return { *ModelCall, Block.ID() };
  }

  model::TypePath
  getCallSitePrototype(const model::Binary &Binary,
                       CallInst Call,
                       const model::Function *ParentFunction = nullptr) {
    if (not ParentFunction)
      ParentFunction = Traits::getModelFunction(Binary,
                                                Traits::getFunction(Call));

    if (not ParentFunction)
      return {};

    const auto &[Edge, BlockAddress] = getCallEdge(Binary, Call);

    if (not Edge)
      return {};

    return getPrototype(Binary, ParentFunction->Entry(), BlockAddress, *Edge);
  }
};

struct MLIRTraits {
  using BasicBlock = mlir::Block *;
  using Value = mlir::Operation *;
  using Function = LLVMFuncOp;
  using CallInst = LLVMCallOp;
  using Location = pipeline::Location<decltype(revng::ranks::Instruction)>;

  static LLVMFuncOp getParent(mlir::Block *const B) {
    mlir::Operation *const Parent = B->getParentOp();
    auto F = mlir::dyn_cast<LLVMFuncOp>(Parent);

    if (not F)
      F = getFunction(Parent);

    return F;
  }

  static mlir::Block *getParent(mlir::Operation *const O) {
    return O->getBlock();
  }

  static LLVMFuncOp getFunction(mlir::Operation *const O) {
    return O->getParentOfType<LLVMFuncOp>();
  }

  static std::optional<Location> getLocation(mlir::Operation *const O) {
    auto MaybeLoc = mlir::dyn_cast_or_null<LocType>(O->getLoc());

    if (not MaybeLoc)
      return std::nullopt;

    return Location::fromString(MaybeLoc.getMetadata().getName().getValue());
  }

  static TupleTree<efa::FunctionMetadata>
  extractFunctionMetadata(LLVMFuncOp F) {
    auto Attr = F->getAttrOfType<mlir::StringAttr>(FunctionMetadataMDName);
    const llvm::StringRef YAMLString = Attr.getValue();
    auto
      MaybeParsed = TupleTree<efa::FunctionMetadata>::deserialize(YAMLString);
    revng_assert(MaybeParsed and MaybeParsed->verify());
    return std::move(MaybeParsed.get());
  }

  static const model::Function *getModelFunction(const model::Binary &Binary,
                                                 LLVMFuncOp F) {
    auto const FI = mlir::cast<mlir::FunctionOpInterface>(F.getOperation());
    auto const MaybeFunctionName = getFunctionName(FI);

    if (not MaybeFunctionName)
      return nullptr;

    const auto MA = MetaAddress::fromString(*MaybeFunctionName);
    const auto It = Binary.Functions().find(MA);

    if (It == Binary.Functions().end())
      return nullptr;

    return &*It;
  }
};
using MLIRFunctionMetadataCache = BasicFunctionMetadataCache<MLIRTraits>;

class ImportCliftTypesPipe {
public:
  static constexpr auto Name = "import-clift-types";

  std::array<pipeline::ContractGroup, 1> getContract() const {
    using namespace pipeline;
    using namespace revng::kinds;

    return { ContractGroup({ Contract(MLIRLLVMFunctionKind,
                                      0,
                                      MLIRLLVMFunctionKind,
                                      0,
                                      InputPreservation::Preserve) }) };
  }

  void run(const pipeline::ExecutionContext &Ctx,
           revng::pipes::MLIRContainer &MLIRContainer) {
    const TupleTree<model::Binary> &Model = revng::getModelFromContext(Ctx);

    mlir::ModuleOp Module = MLIRContainer.getModule();
    mlir::MLIRContext &Context = *Module.getContext();

    llvm::DenseSet<const model::Type *> ImportedTypes;

    mlir::OwningOpRef<mlir::ModuleOp>
      NewModule = mlir::ModuleOp::create(mlir::UnknownLoc::get(&Context));
    mlir::OpBuilder Builder(NewModule->getRegion());

    const auto importFunctionPrototype = [&](const model::Type &ModelType) {
      if (not ImportedTypes.insert(&ModelType).second)
        return;

      if (ImportedTypes.size() == 1) {
        mlir::DialectRegistry Registry;
        Registry.insert<mlir::clift::CliftDialect>();
        Context.appendDialectRegistry(Registry);
        Context.loadAllAvailableDialects();
      }

      const auto EmitError = [&]() -> mlir::InFlightDiagnostic {
        return Context.getDiagEngine().emit(mlir::UnknownLoc::get(&Context),
                                            mlir::DiagnosticSeverity::Error);
      };

      const auto CliftType = revng::getUnqualifiedTypeChecked(EmitError,
                                                              Context,
                                                              ModelType);
      revng_assert(CliftType);

      Builder.create<mlir::clift::UndefOp>(mlir::UnknownLoc::get(&Context),
                                           CliftType);
    };

    MLIRFunctionMetadataCache Cache;
    Module->walk([&](mlir::FunctionOpInterface F) {
      const auto Name = getFunctionName(F);

      if (not Name)
        return;

      const auto &ModelFunctions = Model->Functions();
      const auto It = ModelFunctions.find(MetaAddress::fromString(*Name));
      revng_assert(It != ModelFunctions.end());
      const model::Function &ModelFunction = *It;

      importFunctionPrototype(*ModelFunction.Prototype().getConst());

      if (F.isExternal())
        return;

      F->walk([&](LLVMCallOp Call) {
        const auto CalleePrototype = Cache.getCallSitePrototype(*Model,
                                                                Call,
                                                                &ModelFunction);

        if (CalleePrototype.empty())
          return;

        importFunctionPrototype(*CalleePrototype.getConst());
      });
    });

    auto &OldBlock = getModuleBlock(Module);
    auto &NewBlock = getModuleBlock(*NewModule);

    while (not NewBlock.empty()) {
      mlir::Operation &NewOperation = NewBlock.front();
      NewOperation.remove();
      OldBlock.push_back(&NewOperation);
    }
  }
};

static pipeline::RegisterPipe<ImportCliftTypesPipe> X;
} // namespace
