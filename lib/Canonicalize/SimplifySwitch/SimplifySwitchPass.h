#pragma once

//
// Copyright rev.ng Labs Srl. See LICENSE.md for details.
//

#include "llvm/Pass.h"

struct SimplifySwitchPass : public llvm::FunctionPass {
public:
  static char ID;

  SimplifySwitchPass();

  bool runOnFunction(llvm::Function &F) override;

  void getAnalysisUsage(llvm::AnalysisUsage &AU) const override;
};
