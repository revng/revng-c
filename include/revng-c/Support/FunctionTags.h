#pragma once

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include <compare>
namespace llvm {
class Function;
class LLVMContext;
class Module;
class Type;
class ExtractValueInst;
} // end namespace llvm

#include "revng/Support/FunctionTags.h"
#include "revng/Support/OpaqueFunctionsPool.h"

namespace FunctionTags {

inline const char *StackTypeMDName = "revng.stack_type";
inline const char *VariableTypeMDName = "revng.variable_type";

extern Tag AllocatesLocalVariable;
extern Tag ReturnsPolymorphic;
extern Tag IsRef;
extern Tag AddressOf;
extern Tag StringLiteral;
extern Tag ModelCast;
extern Tag ModelGEP;
extern Tag ModelGEPRef;
extern Tag OpaqueExtractValue;
extern Tag Parentheses;
extern Tag LiteralPrintDecorator;
extern Tag HexInteger;
extern Tag CharInteger;
extern Tag BoolInteger;
extern Tag NullPtr;
extern Tag LocalVariable;
extern Tag Assign;
extern Tag Copy;
extern Tag SegmentRef;
extern Tag UnaryMinus;
extern Tag BinaryNot;
extern Tag BooleanNot;

inline Tag LiftingArtifactsRemoved("lifting-artifacts-removed", CSVsPromoted);

inline Tag StackPointerPromoted("stack-pointer-promoted",
                                LiftingArtifactsRemoved);

inline Tag StackAccessesSegregated("stack-accesses-segregated",
                                   StackPointerPromoted);

inline Tag Decompiled("decompiled", StackPointerPromoted);

inline Tag StackOffsetMarker("stack-offset-marker");

inline Tag BinaryOperationOverflows("binary-operation-overflow");

} // namespace FunctionTags

/// This struct can be used as a key of an OpaqueFunctionsPool where both
/// the return type and one of the arguments are needed to identify a function
/// in the pool.
struct TypePair {
  llvm::Type *RetType;
  llvm::Type *ArgType;

  bool operator<(const TypePair &Rhs) const {
    return RetType < Rhs.RetType
           or (RetType == Rhs.RetType and ArgType < Rhs.ArgType);
  }
};

/// AddressOf functions are used to transform a reference into a pointer.
///
/// \param RetType The LLVM type returned by the Addressof call
/// \param BaseType The LLVM type of the second argument (the reference that
/// we want to transform into a pointer).
llvm::FunctionType *getAddressOfType(llvm::Type *RetType, llvm::Type *BaseType);

/// Creates a pool of AddressOf functions, initializing its internal state
/// from the Module \a M
OpaqueFunctionsPool<TypePair> makeAddressOfPool(llvm::Module &M);

struct StringLiteralPoolKey {
  MetaAddress Address;
  uint64_t VirtualSize;
  uint64_t OffsetInSegment;
  llvm::Type *Type;

  std::strong_ordering
  operator<=>(const StringLiteralPoolKey &) const = default;
};

/// Creates a pool of StringLiteral functions, initializing its internal state
/// from the Module \a M
OpaqueFunctionsPool<StringLiteralPoolKey>
makeStringLiteralPool(llvm::Module &M);

/// Creates a pool of Parentheses functions, initializing its internal state
/// from the Module \a M
OpaqueFunctionsPool<llvm::Type *> makeParenthesesPool(llvm::Module &M);

/// Creates a pool of HexInteger functions, initializing its internal state
/// from the Module \a M
OpaqueFunctionsPool<llvm::Type *> makeHexPrintPool(llvm::Module &M);

/// Creates a pool of CharInteger functions, initializing its internal state
/// from the Module \a M
OpaqueFunctionsPool<llvm::Type *> makeCharPrintPool(llvm::Module &M);

/// Creates a pool of BoolInteger functions, initializing its internal state
/// from the Module \a M
OpaqueFunctionsPool<llvm::Type *> makeBoolPrintPool(llvm::Module &M);

/// Creates a pool of NullPtr functions, initializing its internal state
/// from the Module \a M
OpaqueFunctionsPool<llvm::Type *> makeNullPtrPrintPool(llvm::Module &M);

/// Creates a pool of UnaryMinus functions, initializing its internal state
/// from the Module \a M
OpaqueFunctionsPool<llvm::Type *> makeUnaryMinusPool(llvm::Module &M);

/// Creates a pool of BinaryNot functions, initializing its internal state
/// from the Module \a M
OpaqueFunctionsPool<llvm::Type *> makeBinaryNotPool(llvm::Module &M);

/// Creates a pool of BooleanNot functions, initializing its internal state
/// from the Module \a M
OpaqueFunctionsPool<llvm::Type *> makeBooleanNotPool(llvm::Module &M);

/// ModelGEP functions are used to replace pointer arithmetic with a navigation
/// of the Model.
///
/// \param RetType ModelGEP should return an integer of the size of the gepped
/// element
/// \param BaseType The LLVM type of the second argument (the base pointer)
llvm::Function *
getModelGEP(llvm::Module &M, llvm::Type *RetType, llvm::Type *BaseType);

/// ModelGEP Ref is a ModelGEP where the base value is considered to be a
/// reference.
llvm::Function *
getModelGEPRef(llvm::Module &M, llvm::Type *RetType, llvm::Type *BaseType);

using ModelCastPoolKey = std::pair<llvm::Type *, llvm::Type *>;

/// Creates a pool of ModelCast functions, initializing its internal
/// state from the Module \a M
OpaqueFunctionsPool<TypePair> makeModelCastPool(llvm::Module &M);

using SegmentRefPoolKey = std::tuple<MetaAddress, uint64_t, llvm::Type *>;

/// Creates a pool of SegmentRef functions, initializing its internal
/// state from the Module \a M
OpaqueFunctionsPool<SegmentRefPoolKey> makeSegmentRefPool(llvm::Module &M);

/// Derive the function type of the corresponding OpaqueExtractValue() function
/// from an ExtractValue instruction. OpaqueExtractValues wrap an
/// ExtractValue to prevent it from being optimized out, so the return type and
/// arguments are the same as the instruction being wrapped.
llvm::FunctionType *getOpaqueEVFunctionType(llvm::ExtractValueInst *Extract);

/// Creates a pool of OpaqueExtractValue functions, initializing its internal
/// state from the Module \a M
OpaqueFunctionsPool<TypePair> makeOpaqueEVPool(llvm::Module &M);

/// LocalVariable is used to indicate the allocation of a local variable. It
/// returns a reference to the allocated variable.
llvm::FunctionType *getLocalVarType(llvm::Type *ReturnedType);

/// Creates a pool of LocalVariable functions, initializing its internal state
/// from the Module \a M
OpaqueFunctionsPool<llvm::Type *> makeLocalVarPool(llvm::Module &M);

/// Assign() are meant to replace `store` instructions in which the pointer
/// operand is a reference.
llvm::FunctionType *getAssignFunctionType(llvm::Type *ValueType,
                                          llvm::Type *PtrType);

/// Creates a pool of Assign functions, initializing its internal state
/// from the Module \a M
OpaqueFunctionsPool<llvm::Type *> makeAssignPool(llvm::Module &M);

/// Copy() are meant to replace `load` instructions in which the pointer
/// operand is a reference.
llvm::FunctionType *getCopyType(llvm::Type *ReturnedType);

/// Creates a pool of Copy functions, initializing its internal state
/// from the Module \a M
OpaqueFunctionsPool<llvm::Type *> makeCopyPool(llvm::Module &M);
