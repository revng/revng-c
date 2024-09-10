#pragma once

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

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

  /// @}

public:
  ~LocalVariableHelper() = default;

  LocalVariableHelper(const model::Binary &TheBinary,
                      llvm::Module &TheModule,
                      bool Legacy) :
    Binary(TheBinary),
    M(TheModule),
    PtrSizedInteger(getPointerSizedInteger(M.getContext(), Binary)),
    Int8Ty(llvm::Type::getInt8Ty(M.getContext())),
    F(nullptr),
    IsLegacy(Legacy) {}

  LocalVariableHelper(const LocalVariableHelper &) = default;

  LocalVariableHelper(LocalVariableHelper &&) = default;

  bool isLegacy() const { return IsLegacy; }

public:
  void setTargetFunction(llvm::Function *NewF) { F = NewF; }

  llvm::Instruction *createLocalVariable(const model::Type &VariableType) const;

  llvm::Instruction *createStackFrameVariable() const;

private:
  /// Legacy methods for creating local variables.
  /// They should only be called with `IsLegacy` is true.
  /// TODO: drop these when we drop legacy mode
  /// @{

  llvm::Instruction *
  createLegacyLocalVariable(const model::Type &VariableType) const;

  llvm::Instruction *createLegacyStackFrameVariable() const;

  /// @}
};
