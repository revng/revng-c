#pragma once

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

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

  bool IsLegacy;

public:
  ~LocalVariableHelper() = default;

  LocalVariableHelper(bool Legacy) : IsLegacy(Legacy) {}
  LocalVariableHelper() : LocalVariableHelper(false) {}

  LocalVariableHelper(const LocalVariableHelper &) = default;
  LocalVariableHelper &operator=(const LocalVariableHelper &) = default;

  LocalVariableHelper(LocalVariableHelper &&) = default;
  LocalVariableHelper &operator=(LocalVariableHelper &&) = default;

  bool isLegacy() const { return IsLegacy; }
};
