#pragma once

//
// Copyright rev.ng Labs Srl. See LICENSE.md for details.
//

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/ModRef.h"

#include "revng/Support/IRHelpers.h"

/// This is a helper class used to create the markers in the basic blocks needed
/// to handle virtual edges, such as the scope closer edges or the goto edges.
/// Its role should be similar somewhat to the `llvm::IRBuilder` role.
class MarkerCallBuilder {
private:
  llvm::BasicBlock *BB;
  llvm::Function *MarkerCallFunction;

public:
  MarkerCallBuilder(std::string MarkerCallName, llvm::Function *F) {

    // Create or get the marker function declaration, that will be called by the
    // inserted markers
    llvm::LLVMContext &C = getContext(F);
    llvm::Module *M = getModule(F);
    llvm::Type *BlockAddressTy = llvm::Type::getInt8PtrTy(C);
    auto *FT = llvm::FunctionType::get(llvm::Type::getVoidTy(C),
                                       { BlockAddressTy },
                                       false);

    using llvm::Function;
    using llvm::GlobalValue;
    MarkerCallFunction = llvm::cast<
      llvm::Function>(M->getOrInsertFunction(MarkerCallName, FT).getCallee());
    MarkerCallFunction->setLinkage(llvm::GlobalValue::ExternalLinkage);
    MarkerCallFunction->addFnAttr(llvm::Attribute::OptimizeNone);
    MarkerCallFunction->addFnAttr(llvm::Attribute::NoInline);
    MarkerCallFunction->addFnAttr(llvm::Attribute::NoMerge);
    MarkerCallFunction->addFnAttr(llvm::Attribute::NoUnwind);
    MarkerCallFunction->addFnAttr(llvm::Attribute::WillReturn);
    using llvm::MemoryEffects;
    MarkerCallFunction->setMemoryEffects(MemoryEffects::inaccessibleMemOnly());
  }

public:
  // Set the insertion point of the `MarkerCallBuilder`
  void setInsertPoint(llvm::BasicBlock *NewBB) { BB = NewBB; }

  void insertMarkerCall(llvm::BasicBlock *BasicBlockTarget) {

    // We must have an insertion point
    revng_assert(BB);

    // We assume that when inserting a `goto` edge, the original block had a
    // single regular successor on the CFG
    if (MarkerCallFunction->getName() == "goto_target") {
      llvm::Instruction *Terminator = BB->getTerminator();
      revng_assert(Terminator->getNumSuccessors() == 1);
    }

    // We always insert the marker as the penultimate instruction in a
    // `BasicBlock`, regardless of the type of the marker we are inserting
    llvm::Instruction *Terminator = BB->getTerminator();
    llvm::IRBuilder<> Builder(Terminator);
    auto *BasicBlockAddressTarget = llvm::BlockAddress::get(BasicBlockTarget);
    revng_assert(BasicBlockAddressTarget);
    Builder.CreateCall(MarkerCallFunction, BasicBlockAddressTarget);
  }
};

/// Helper method to retrieve the `BasicBlock` target of the marker
inline llvm::BasicBlock *getMarkerCallTarget(std::string MarkerCallName,
                                             llvm::BasicBlock *BB) {

  // We must be provided with a `BasicBlock` where to search for the marker
  revng_assert(BB);

  // When using the getter, we assume that the marker function declaration is
  // present in the `Module`
  llvm::Module *M = getModule(BB);
  llvm::Function *MarkerCallFunction = M->getFunction(MarkerCallName);
  revng_assert(MarkerCallFunction);

  // We assume that the call to the marker function can be in the last but one,
  // or last but two position in the `BasicBlock`, without assuming a particular
  // order between the marker call functions having a different type
  auto BBIt = BB->rbegin();
  ++BBIt;
  for (size_t Index = 0; Index < 2 && BBIt != BB->rend(); ++BBIt) {
    llvm::Instruction &TentativeInst = *BBIt;
    if (llvm::CallInst *MarkerCall = getCallTo(&TentativeInst,
                                               MarkerCallFunction)) {
      auto *MarkerCallTargetBlockAddress = llvm::cast<
        llvm::BlockAddress>(MarkerCall->getArgOperand(0));
      using llvm::BasicBlock;
      auto *MarkerCallTargetBB = MarkerCallTargetBlockAddress->getBasicBlock();
      return MarkerCallTargetBB;
    }
  }

  return nullptr;
}

inline llvm::BasicBlock *getScopeCloser(llvm::BasicBlock *BB) {
  return getMarkerCallTarget("scope_closer", BB);
}

inline llvm::BasicBlock *getGotoTarget(llvm::BasicBlock *BB) {
  return getMarkerCallTarget("goto_target", BB);
}

inline void verifyBasicBlock(std::string MarkerCallName, llvm::BasicBlock *BB) {
  llvm::Module *M = getModule(BB);
  llvm::Function *MarkerCallFunction = M->getFunction(MarkerCallName);
  revng_assert(MarkerCallFunction);

  // We should find at maximum one occurrence of the call to the marker
  // function in either the last but one or last but two position in the
  // `BasicBlock`
  bool MarkerFound = false;
  auto BBIt = BB->rbegin();
  ++BBIt;
  for (size_t Index = 0; Index < 2 && BBIt != BB->rend(); ++BBIt) {
    llvm::Instruction &TentativeInst = *BBIt;
    if (llvm::CallInst *MarkerCall = getCallTo(&TentativeInst,
                                               MarkerCallFunction)) {
      revng_assert(MarkerFound == false, "Duplicate Marker Call");
      MarkerFound = true;
    }
  }

  // In the rest of the `BasicBlock`, we should not find any call to the
  // marker function
  for (; BBIt != BB->rend(); ++BBIt) {
    llvm::Instruction &TentativeInst = *BBIt;
    if (llvm::CallInst *MarkerCall = getCallTo(&TentativeInst,
                                               MarkerCallFunction)) {
      revng_abort("No Marker Call expected");
    }
  }
}
