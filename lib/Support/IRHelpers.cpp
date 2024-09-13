//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include <type_traits>

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Metadata.h"

#include "revng/Support/Assert.h"
#include "revng/Support/MetaAddress.h"

#include "revng-c/Support/FunctionTags.h"
#include "revng-c/Support/IRHelpers.h"
#include "revng-c/Support/ModelHelpers.h"

template<typename T>
concept DerivedValue = std::is_base_of_v<llvm::Value, T>;

using std::conditional_t;

template<DerivedValue ConstnessT, DerivedValue ResultT>
using PossiblyConstValueT = conditional_t<std::is_const_v<ConstnessT>,
                                          std::add_const_t<ResultT>,
                                          std::remove_const_t<ResultT>>;

template<DerivedValue T>
using ValueT = PossiblyConstValueT<T, llvm::Value>;

template<DerivedValue T>
using CallT = PossiblyConstValueT<T, llvm::CallInst>;

template<DerivedValue T>
using CallPtrSet = llvm::SmallPtrSet<CallT<T> *, 2>;

template<DerivedValue T>
llvm::SmallVector<CallPtrSet<T>, 2>
getConstQualifiedExtractedValuesFromInstruction(T *I) {

  llvm::SmallVector<CallPtrSet<T>, 2> Results;

  auto *StructTy = llvm::cast<llvm::StructType>(I->getType());
  unsigned NumFields = StructTy->getNumElements();
  Results.resize(NumFields, {});

  // Find extract value uses transitively, traversing PHIs and markers
  CallPtrSet<T> Calls;
  for (auto *TheUser : I->users()) {
    if (auto *ExtractV = getCallToTagged(TheUser,
                                         FunctionTags::OpaqueExtractValue)) {
      Calls.insert(ExtractV);
    } else {
      if (auto *Call = dyn_cast<llvm::CallInst>(TheUser)) {
        if (not isCallToTagged(Call, FunctionTags::Parentheses))
          continue;
      }

      // traverse PHIS and markers until we find extractvalues
      llvm::SmallPtrSet<ValueT<T> *, 8> Visited = {};
      llvm::SmallPtrSet<ValueT<T> *, 8> ToVisit = { TheUser };
      while (not ToVisit.empty()) {

        llvm::SmallPtrSet<ValueT<T> *, 8> NextToVisit = {};

        for (ValueT<T> *Ident : ToVisit) {
          Visited.insert(Ident);
          NextToVisit.erase(Ident);

          for (auto *User : Ident->users()) {
            using FunctionTags::OpaqueExtractValue;
            if (auto *EV = getCallToTagged(User, OpaqueExtractValue)) {
              Calls.insert(EV);
            } else if (auto *IdentUser = llvm::dyn_cast<llvm::CallInst>(User)) {
              if (isCallToTagged(IdentUser, FunctionTags::Parentheses))
                NextToVisit.insert(IdentUser);
            } else if (auto *PHIUser = llvm::dyn_cast<llvm::PHINode>(User)) {
              if (not Visited.contains(PHIUser))
                NextToVisit.insert(PHIUser);
            }
          }
        }

        ToVisit = NextToVisit;
      }
    }
  }

  for (auto *E : Calls) {
    revng_assert(isa<llvm::IntegerType>(E->getType())
                 or isa<llvm::PointerType>(E->getType()));
    auto FieldId = cast<llvm::ConstantInt>(E->getArgOperand(1))->getZExtValue();
    Results[FieldId].insert(E);
  }

  return Results;
};

llvm::SmallVector<llvm::SmallPtrSet<llvm::CallInst *, 2>, 2>
getExtractedValuesFromInstruction(llvm::Instruction *I) {
  return getConstQualifiedExtractedValuesFromInstruction(I);
}

llvm::SmallVector<llvm::SmallPtrSet<const llvm::CallInst *, 2>, 2>
getExtractedValuesFromInstruction(const llvm::Instruction *I) {
  return getConstQualifiedExtractedValuesFromInstruction(I);
}

bool deleteOnlyBody(llvm::Function &F) {
  bool Result = false;
  if (not F.empty()) {
    // deleteBody() also kills all attributes and tags. Since we still
    // want them, we have to save them and re-add them after deleting the
    // body of the function.
    auto Attributes = F.getAttributes();
    auto FTags = FunctionTags::TagsSet::from(&F);

    llvm::SmallVector<std::pair<unsigned, llvm::MDNode *>> AllMetadata;
    if (F.hasMetadata())
      F.getAllMetadata(AllMetadata);

    // Kill the body.
    F.deleteBody();

    // Restore tags and attributes
    FTags.set(&F);
    F.setAttributes(Attributes);

    F.clearMetadata();
    for (const auto &[KindID, MetaData] : AllMetadata) {
      // Debug metadata is not stripped away by deleteBody() nor by
      // clearMetadata(), but it is wrong to set it twice (the Module would not
      // verify anymore). Hence set the metadata only if its not a debug
      // metadata.
      if (not F.hasMetadata(KindID) and KindID != llvm::LLVMContext::MD_dbg)
        F.setMetadata(KindID, MetaData);
    }

    Result = true;
  }
  return Result;
}

void setSegmentKeyMetadata(llvm::Function &SegmentRefFunction,
                           MetaAddress StartAddress,
                           uint64_t VirtualSize) {
  using namespace llvm;

  auto &Context = SegmentRefFunction.getContext();

  QuickMetadata QMD(Context);

  auto *SAMD = QMD.get(StartAddress.toString());
  revng_assert(SAMD != nullptr);

  auto *VSConstant = ConstantInt::get(Type::getInt64Ty(Context), VirtualSize);
  auto *VSMD = ConstantAsMetadata::get(VSConstant);

  SegmentRefFunction.setMetadata(FunctionTags::UniqueIDMDName,
                                 QMD.tuple({ SAMD, VSMD }));
}

bool hasSegmentKeyMetadata(const llvm::Function &F) {
  auto &Ctx = F.getContext();
  auto SegmentRefMDKind = Ctx.getMDKindID(FunctionTags::UniqueIDMDName);
  return nullptr != F.getMetadata(SegmentRefMDKind);
}

std::pair<MetaAddress, uint64_t>
extractSegmentKeyFromMetadata(const llvm::Function &F) {
  using namespace llvm;
  revng_assert(hasSegmentKeyMetadata(F));

  auto &Ctx = F.getContext();

  auto SegmentRefMDKind = Ctx.getMDKindID(FunctionTags::UniqueIDMDName);
  auto *Node = F.getMetadata(SegmentRefMDKind);

  auto *SAMD = cast<MDString>(Node->getOperand(0));
  MetaAddress StartAddress = MetaAddress::fromString(SAMD->getString());
  auto *VSMD = cast<ConstantAsMetadata>(Node->getOperand(1))->getValue();
  uint64_t VirtualSize = cast<ConstantInt>(VSMD)->getZExtValue();

  return { StartAddress, VirtualSize };
}

void setStackTypeMetadata(llvm::Instruction *I, const model::Type &StackType) {
  llvm::Constant *
    StackTypeStringConstant = serializeToLLVMString(StackType, *I->getModule());
  using CAM = llvm::ConstantAsMetadata;
  auto &Context = I->getContext();
  QuickMetadata QMD(Context);
  I->setMetadata(FunctionTags::StackTypeMDName,
                 QMD.tuple({ CAM::get(StackTypeStringConstant) }));
}

bool hasStackTypeMetadata(const llvm::Instruction *I) {
  auto &C = I->getContext();
  auto StackTypeMDKind = C.getMDKindID(FunctionTags::StackTypeMDName);
  return nullptr != I->getMetadata(StackTypeMDKind);
}

model::UpcastableType getStackTypeFromMetadata(llvm::Instruction *I,
                                               const model::Binary &Model) {
  revng_assert(hasStackTypeMetadata(I));

  auto &C = I->getContext();
  auto StackTypeMDKind = C.getMDKindID(FunctionTags::StackTypeMDName);
  auto *Node = I->getMetadata(StackTypeMDKind);

  using CAM = llvm::ConstantAsMetadata;
  auto *StackTypeStringConstant = cast<CAM>(Node->getOperand(0))->getValue();
  auto StackType = deserializeFromLLVMString(StackTypeStringConstant, Model);
  revng_assert(not StackType.isEmpty());
  return StackType;
}

void setVariableTypeMetadata(llvm::Instruction *I,
                             const model::Type &VariableType) {
  llvm::Constant
    *VarTypeStringConstant = serializeToLLVMString(VariableType,
                                                   *I->getModule());
  using CAM = llvm::ConstantAsMetadata;
  auto &Context = I->getContext();
  QuickMetadata QMD(Context);
  I->setMetadata(FunctionTags::VariableTypeMDName,
                 QMD.tuple({ CAM::get(VarTypeStringConstant) }));
}

bool hasVariableTypeMetadata(const llvm::Instruction *I) {
  auto &C = I->getContext();
  auto VariableTypeMDKind = C.getMDKindID(FunctionTags::VariableTypeMDName);
  return nullptr != I->getMetadata(VariableTypeMDKind);
}

model::UpcastableType getVariableTypeFromMetadata(llvm::Instruction *I,
                                                  const model::Binary &Model) {
  revng_assert(hasVariableTypeMetadata(I));

  auto &C = I->getContext();
  auto VariableTypeMDKind = C.getMDKindID(FunctionTags::VariableTypeMDName);
  auto *Node = I->getMetadata(VariableTypeMDKind);

  using CAM = llvm::ConstantAsMetadata;
  auto *VarTypeStringConstant = cast<CAM>(Node->getOperand(0))->getValue();
  auto VarType = deserializeFromLLVMString(VarTypeStringConstant, Model);
  revng_assert(not VarType.isEmpty());
  return VarType;
}

void setStringLiteralMetadata(llvm::Function &StringLiteralFunction,
                              MetaAddress StartAddress,
                              uint64_t VirtualSize,
                              uint64_t Offset,
                              uint64_t StringLength,
                              llvm::Type *ReturnType) {
  using namespace llvm;

  auto *M = StringLiteralFunction.getParent();
  auto &Ctx = StringLiteralFunction.getContext();

  QuickMetadata QMD(M->getContext());
  auto StringLiteralMDKind = Ctx.getMDKindID(FunctionTags::UniqueIDMDName);

  auto *SAMD = QMD.get(StartAddress.toString());

  auto *VSConstant = ConstantInt::get(Type::getInt64Ty(Ctx), VirtualSize);
  auto *VSMD = ConstantAsMetadata::get(VSConstant);

  auto *OffsetConstant = ConstantInt::get(Type::getInt64Ty(Ctx), Offset);
  auto *OffsetMD = ConstantAsMetadata::get(OffsetConstant);

  auto *StrLenConstant = ConstantInt::get(Type::getInt64Ty(Ctx), StringLength);
  auto *StrLenMD = ConstantAsMetadata::get(StrLenConstant);

  unsigned Value = ReturnType->isPointerTy() ? 0 :
                                               ReturnType->getIntegerBitWidth();
  auto *ReturnTypeConstant = ConstantInt::get(Type::getInt64Ty(Ctx), Value);
  auto *ReturnTypeMD = ConstantAsMetadata::get(ReturnTypeConstant);

  auto QMDTuple = QMD.tuple({ SAMD, VSMD, OffsetMD, StrLenMD, ReturnTypeMD });
  StringLiteralFunction.setMetadata(StringLiteralMDKind, QMDTuple);
}

bool hasStringLiteralMetadata(const llvm::Function &F) {
  auto &Ctx = F.getContext();
  auto StringLiteralMDKind = Ctx.getMDKindID(FunctionTags::UniqueIDMDName);
  return nullptr != F.getMetadata(StringLiteralMDKind);
}

std::tuple<MetaAddress, uint64_t, uint64_t, uint64_t, llvm::Type *>
extractStringLiteralFromMetadata(const llvm::Function &F) {
  using namespace llvm;
  revng_assert(hasStringLiteralMetadata(F));

  auto &Ctx = F.getContext();

  auto StringLiteralMDKind = Ctx.getMDKindID(FunctionTags::UniqueIDMDName);
  auto *Node = F.getMetadata(StringLiteralMDKind);

  StringRef SAMD = cast<MDString>(Node->getOperand(0))->getString();
  MetaAddress StartAddress = MetaAddress::fromString(SAMD);

  auto ExtractInteger = [](const MDOperand &Operand) {
    auto *MD = cast<ConstantAsMetadata>(Operand)->getValue();
    return cast<ConstantInt>(MD)->getZExtValue();
  };

  uint64_t VirtualSize = ExtractInteger(Node->getOperand(1));
  uint64_t Offset = ExtractInteger(Node->getOperand(2));
  uint64_t StrLen = ExtractInteger(Node->getOperand(3));
  uint64_t ReturnTypeLength = ExtractInteger(Node->getOperand(4));
  llvm::Type *PointerType = llvm::PointerType::get(Ctx, 0);
  llvm::Type *ReturnType = ReturnTypeLength == 0 ?
                             PointerType :
                             llvm::IntegerType::get(Ctx, ReturnTypeLength);

  return { StartAddress, VirtualSize, Offset, StrLen, ReturnType };
}

void emitMessage(llvm::Instruction *EmitBefore, const llvm::Twine &Message) {
  llvm::IRBuilder<> Builder(EmitBefore);
  emitMessage(Builder, Message);
}

void emitMessage(llvm::IRBuilder<> &Builder, const llvm::Twine &Message) {
  using namespace llvm;

  Module *M = getModule(Builder.GetInsertBlock());
  auto *FT = createFunctionType<void, const uint8_t *>(M->getContext());
  // TODO: use reserved prefix
  llvm::StringRef MessageFunctionName("revng_message");
  FunctionCallee Callee = M->getOrInsertFunction(MessageFunctionName, FT);

  Function *F = cast<Function>(Callee.getCallee());
  if (not FunctionTags::Helper.isTagOf(F))
    FunctionTags::Helper.addTo(F);

  Builder.CreateCall(Callee, getUniqueString(M, "emitMessage", Message.str()));
}
