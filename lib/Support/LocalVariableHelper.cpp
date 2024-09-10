//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Module.h"

#include "revng/Model/Binary.h"
#include "revng/Model/Type.h"
#include "revng/Support/Assert.h"
#include "revng/Support/IRHelpers.h"

#include "revng-c/Support/FunctionTags.h"
#include "revng-c/Support/IRHelpers.h"
#include "revng-c/Support/LocalVariableHelper.h"
#include "revng-c/Support/ModelHelpers.h"

using namespace llvm;
using LVH = LocalVariableHelper;

static const model::Function &getModelFunction(llvm::Function *F,
                                               const model::Binary &Binary) {
  MetaAddress Entry = getMetaAddressMetadata(F, "revng.function.entry");
  revng_assert(Entry.isValid());
  return Binary.Functions().at(Entry);
}

Instruction *LVH::createLocalVariable(const model::Type &VariableType) const {

  if (isLegacy())
    return createLegacyLocalVariable(VariableType);

  size_t VariableSize = VariableType.size().value_or(0);
  revng_assert(VariableSize);

  IRBuilder<> B(F->getContext());
  setInsertPointToFirstNonAlloca(B, *F);

  auto *AllocaLocalVariable = B.CreateAlloca(Int8Ty, VariableSize);
  setVariableTypeMetadata(AllocaLocalVariable, VariableType);

  return cast<Instruction>(B.CreatePtrToInt(AllocaLocalVariable,
                                            PtrSizedInteger));
}

Instruction *
LVH::createLegacyLocalVariable(const model::Type &VariableType) const {

  revng_abort("TODO: createLegacyLocalVariable not implemented yet");
  return nullptr;
}

Instruction *LVH::createStackFrameVariable() const {

  if (isLegacy())
    return createLegacyStackFrameVariable();

  const model::Function &ModelFunction = getModelFunction(F, Binary);
  model::UpcastableType StackFrameType = ModelFunction.StackFrameType();

  size_t StackSize = StackFrameType->size().value_or(0);
  revng_assert(StackSize);

  IRBuilder<> B(F->getContext());
  setInsertPointToFirstNonAlloca(B, *F);

  auto *AllocaStackFrame = B.CreateAlloca(Int8Ty, StackSize);
  setVariableTypeMetadata(AllocaStackFrame, *StackFrameType.get());

  return cast<Instruction>(B.CreatePtrToInt(AllocaStackFrame, PtrSizedInteger));
}

Instruction *LVH::createLegacyStackFrameVariable() const {

  revng_abort("TODO: createLegacyStackFrameVariable not implemented yet");
  return nullptr;
}
