//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include "llvm/IR/DerivedTypes.h"
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

static void addCommonAttributesAndTags(llvm::Function *F) {
  F->addFnAttr(Attribute::NoUnwind);
  F->addFnAttr(Attribute::WillReturn);
  // NoMerge, because merging two calls to one of these opcodes that
  // allocate local variable would mean merging the variables.
  F->addFnAttr(Attribute::NoMerge);
  F->setMemoryEffects(MemoryEffects::readOnly());
  F->setOnlyAccessesInaccessibleMemory();
  FunctionTags::AllocatesLocalVariable.addTo(F);
  FunctionTags::ReturnsPolymorphic.addTo(F);
  FunctionTags::IsRef.addTo(F);
}

LVH::LocalVariableHelper(const model::Binary &TheBinary,
                         llvm::Module &TheModule,
                         bool Legacy,
                         GlobalValue *StackPointer,
                         OpaqueFunctionsPool<TypePair> *TheAddressOfPool) :
  Binary(TheBinary),
  M(TheModule),
  PtrSizedInteger(getPointerSizedInteger(M.getContext(), Binary)),
  Int8Ty(llvm::Type::getInt8Ty(M.getContext())),
  F(nullptr),
  IsLegacy(Legacy),
  LocalVarPool(makeLocalVarPool(M)),
  AddressOfPool(*TheAddressOfPool) {

  revng_assert((nullptr != TheAddressOfPool) == Legacy);
  revng_assert((nullptr != StackPointer) == Legacy);

  if (not IsLegacy)
    return;

  // Create the function used to represent the allocation of the stack frame
  auto *StackPointerRegType = StackPointer->getValueType();
  auto *SPIntType = cast<llvm::IntegerType>(StackPointerRegType);
  auto *StackFrameAllocatorType = FunctionType::get(SPIntType,
                                                    { SPIntType },
                                                    false);

  StackFrameAllocator = Function::Create(StackFrameAllocatorType,
                                         GlobalValue::ExternalLinkage,
                                         "revng_stack_frame",
                                         &M);
  addCommonAttributesAndTags(StackFrameAllocator);

  // Create the function used to represent the allocation of the stack arguments
  /// for calls to isolated functions.
  llvm::Type *StringPtrType = getStringPtrType(M.getContext());
  auto *StackArgsAllocatorType = FunctionType::get(SPIntType,
                                                   { StringPtrType, SPIntType },
                                                   false);

  CallStackArgumentsAllocator = Function::Create(StackArgsAllocatorType,
                                                 GlobalValue::ExternalLinkage,
                                                 "revng_call_stack_arguments",
                                                 &M);
  addCommonAttributesAndTags(CallStackArgumentsAllocator);
}

static const model::Function &getModelFunction(llvm::Function *F,
                                               const model::Binary &Binary) {
  MetaAddress Entry = getMetaAddressMetadata(F, "revng.function.entry");
  revng_assert(Entry.isValid());
  return Binary.Functions().at(Entry);
}

Instruction *LVH::createLocalVariable(const model::Type &VariableType) {

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

Instruction *LVH::createLegacyLocalVariable(const model::Type &VariableType) {

  auto *LocalVarFunctionType = getLocalVarType(PtrSizedInteger);
  auto *LocalVarFunction = LocalVarPool.get(PtrSizedInteger,
                                            LocalVarFunctionType,
                                            "LocalVariable");

  IRBuilder<> B(F->getContext());
  setInsertPointToFirstNonAlloca(B, *F);
  Constant *ReferenceString = serializeToLLVMString(VariableType, M);
  Instruction *Reference = B.CreateCall(LocalVarFunction, { ReferenceString });

  // Take the address
  llvm::Type *T = Reference->getType();
  auto *AddressOfFunctionType = getAddressOfType(T, T);
  auto *AddressOfFunction = AddressOfPool.get({ T, T },
                                              AddressOfFunctionType,
                                              "AddressOf");
  return cast<Instruction>(B.CreateCall(AddressOfFunction,
                                        { ReferenceString, Reference }));
}

Instruction *
LVH::createCallStackArgumentVariable(const model::Type &VariableType) {
  if (not isLegacy())
    return createLegacyLocalVariable(VariableType);

  size_t VariableSize = VariableType.size().value_or(0);
  revng_assert(VariableSize);

  llvm::Constant *VarTypeString = serializeToLLVMString(VariableType, M);

  auto *StackPointerRegType = CallStackArgumentsAllocator->getReturnType();
  auto *SPIntType = cast<llvm::IntegerType>(StackPointerRegType);

  IRBuilder<> B(F->getContext());
  setInsertPointToFirstNonAlloca(B, *F);

  Instruction *Reference = B.CreateCall(StackFrameAllocator,
                                        { VarTypeString,
                                          ConstantInt::get(SPIntType,
                                                           VariableSize) });
  // Take the address
  llvm::Type *T = Reference->getType();
  auto *AddressOfFunctionType = getAddressOfType(T, T);
  auto *AddressOfFunction = AddressOfPool.get({ T, T },
                                              AddressOfFunctionType,
                                              "AddressOf");

  return cast<Instruction>(B.CreateCall(AddressOfFunction,
                                        { VarTypeString, Reference }));
}

Instruction *LVH::createStackFrameVariable() {

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

Instruction *LVH::createLegacyStackFrameVariable() {

  const model::Function &ModelFunction = getModelFunction(F, Binary);
  model::UpcastableType StackFrameType = ModelFunction.StackFrameType();

  size_t StackSize = StackFrameType->size().value_or(0);
  revng_assert(StackSize);

  auto *StackPointerRegType = StackFrameAllocator->getReturnType();
  auto *SPIntType = cast<llvm::IntegerType>(StackPointerRegType);

  IRBuilder<> B(F->getContext());
  setInsertPointToFirstNonAlloca(B, *F);

  Instruction *Reference = B.CreateCall(StackFrameAllocator,
                                        { ConstantInt::get(SPIntType,
                                                           StackSize) });
  // Take the address
  llvm::Type *T = Reference->getType();
  auto *AddressOfFunctionType = getAddressOfType(T, T);
  auto *AddressOfFunction = AddressOfPool.get({ T, T },
                                              AddressOfFunctionType,
                                              "AddressOf");

  llvm::Constant *StackTypeString = serializeToLLVMString(StackFrameType, M);
  return cast<Instruction>(B.CreateCall(AddressOfFunction,
                                        { StackTypeString, Reference }));
}
