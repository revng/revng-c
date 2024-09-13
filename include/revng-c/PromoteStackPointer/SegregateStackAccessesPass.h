#pragma once

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include "llvm/Pass.h"

/// Rewrite all stack memory accesses
///
/// This pass changes the base address of stack memory access to either:
///
/// * The stack frame of the function (allocated by the `revng_stack_frame`
///   function in legacy mode, otherwise by an alloca annotated with the stack
///   type).
/// * The stack arguments of a call site (allocated by the
///   `revng_call_stack_arguments` function in legacy mode, otherwise by an
///   alloca allocated with the argument type), which is then passed in as the
///   last argument of the function.
/// * The (newly introduced) last argument of the function representing the
///   stack arguments.
///
/// After this pass, all stack accesses have positive offsets and
/// `_init_local_sp` is dropped entirely.
struct SegregateStackAccessesPass : public llvm::ModulePass {
public:
  static char ID;

  /// This pass has two modes of operation:
  /// - when LegacyLocalVariables is true it uses old FunctionTags, and
  ///   dedicated functions to represent local variables and accesses to them
  /// - when LegacyLocalVariables is false it uses regular LLVM
  ///   alloca/load/store instructions to represent local variables and accesses
  ///   to them
  const bool LegacyLocalVariables;

  SegregateStackAccessesPass(bool Legacy) :
    llvm::ModulePass(ID), LegacyLocalVariables(Legacy) {}

  void getAnalysisUsage(llvm::AnalysisUsage &AU) const override;

  bool runOnModule(llvm::Module &M) override;
};
