#pragma once

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include "revng/Support/OpaqueFunctionsPool.h"

#include "revng-c/Support/ModelHelpers.h"

namespace llvm {

class Instruction;
class Function;
class Module;

} // end namespace llvm

namespace model {

class Binary;
class Type;

} // end namespace model

/// Various LLVM Passes in the decompilation pipelines need to create local
/// variables and read/write memory accesses from/to them. In the legacy
/// decompilation pipeline these were represented by dedicated functions, tagged
/// with specific FunctionTags, to represent dedicated opcodes without using
/// LLVM intrinsics. This workaround with FunctionTags and custom opcodes is
/// scheduled to be dropped for the new decompilation pipeline based on the
/// Clift MLIR dialect, which will use regular LLVM alloca/load/store
/// instructions.
///
/// This class provides a bunch of helpers to deal with creation of local
/// variables. The IsLegacy field is used to select at compile-time the
/// appropriate mode of operation:
/// - IsLegacy == true: uses the old FunctionTags and dedicated functions to
///   represent dedicated opcodes
/// - IsLegacy == false: uses regular LLVM alloca/load/store instructions
///
/// TODO: when the migration is over, the IsLegacy field can be dropped to
/// fully embrace the new ways.
class LocalVariableHelper {

  const model::Binary &Binary;

  /// The module that this class manipulates.
  llvm::Module &M;

  /// An LLVM integer type whose size matches the size of a pointer in the
  /// Binary we're decompiling.
  llvm::IntegerType *PtrSizedInteger = nullptr;

  /// An LLVM 8-bits integer
  llvm::IntegerType *Int8Ty = nullptr;

  /// The function where this helper inserts local variables.
  llvm::Function *F;

  /// Data members used only for legacy mode.
  /// TODO: drop these when we drop legacy mode
  /// @{

  const bool IsLegacy = false;

  /// Pool of functions that represent allocation of local variables
  OpaqueFunctionsPool<llvm::Type *> LocalVarPool;

  /// Pool of functions that represent the AddressOf operator.
  /// This is a pointer to a pool, because users of of LocalVariableHelpers
  /// typically want to inject other AddressOf operations, which means that they
  /// need to have a shared pool of AddressOf, in order not to go out of sync.
  OpaqueFunctionsPool<TypePair> &AddressOfPool;

  /// LLVM Function used to represent the allocation of the stack frame.
  llvm::Function *StackFrameAllocator = nullptr;

  /// LLVM Function used to represent the allocation of the stack arguments for
  /// a call to an isolated function.
  llvm::Function *CallStackArgumentsAllocator = nullptr;

  /// @}

private:
  LocalVariableHelper(const model::Binary &TheBinary,
                      llvm::Module &TheModule,
                      bool Legacy,
                      llvm::GlobalValue *StackPointer,
                      OpaqueFunctionsPool<TypePair> *TheAddressOfPool);

public:
  ~LocalVariableHelper() = default;

  LocalVariableHelper(const LocalVariableHelper &) = default;

  LocalVariableHelper(LocalVariableHelper &&) = default;

  bool isLegacy() const { return IsLegacy; }

public:
  /// Factory methods:
  /// @{

  /// Create a LocalVariableHelper in legacy mode.
  /// TODO: drop when legacy mode is obsolete.
  static LocalVariableHelper
  makeLegacy(const model::Binary &TheBinary,
             llvm::Module &TheModule,
             llvm::GlobalValue *StackPointer,
             OpaqueFunctionsPool<TypePair> &TheAddressOfPool) {
    return LocalVariableHelper(TheBinary,
                               TheModule,
                               /* Legacy */ true,
                               StackPointer,
                               &TheAddressOfPool);
  }

  /// Create a LocalVariableHelper in non-legacy mode.
  static LocalVariableHelper make(const model::Binary &TheBinary,
                                  llvm::Module &TheModule) {
    return LocalVariableHelper(TheBinary,
                               TheModule,
                               /* Legacy */ false,
                               /* StackPointer */ nullptr,
                               /* TheAddressOfPool */ nullptr);
  }

  /// @}

public:
  /// Sets the function where the LocalVariableHelper injects instructions
  /// representing local variables.
  void setTargetFunction(llvm::Function *NewF) { F = NewF; }

  /// Creates an llvm::Instruction that models the allocation of a local
  /// variable.
  /// The created instruction is inserted at the beginning of the function F.
  /// This is typically an alloca, but it's a call to LocalVariable in legacy
  /// mode.
  /// TODO: this method can become const when we drop legacy mode.
  llvm::Instruction *createLocalVariable(const model::Type &VariableType);

  /// Creates an llvm::Instruction that models the allocation of a local
  /// variable representing the stack frame.
  /// This is typically an alloca, but it's a call to LocalVariable in legacy
  /// mode.
  /// TODO: this method can become const when we drop legacy mode.
  llvm::Instruction *createStackFrameVariable();

  /// Creates an llvm::Instruction that models the allocation of a local
  /// variable to be passed as a stack argument to a call instruction.
  /// This is typically an alloca, but it's a call to LocalVariable in legacy
  /// mode.
  /// TODO: this method can be dropped when we drop legacy mode, because the
  /// callers can just switch to call createLocalVariable
  llvm::Instruction *
  createCallStackArgumentVariable(const model::Type &VariableType);

private:
  /// Legacy methods for creating local variables.
  /// They should only be called with `IsLegacy` is true.
  /// TODO: drop these when we drop legacy mode
  /// @{

  llvm::Instruction *createLegacyLocalVariable(const model::Type &VariableType);

  llvm::Instruction *createLegacyStackFrameVariable();

  /// @}
};
