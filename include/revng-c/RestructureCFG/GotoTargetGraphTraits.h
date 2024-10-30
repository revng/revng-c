#pragma once

//
// Copyright rev.ng Labs Srl. See LICENSE.md for details.
//

#include "llvm/ADT/GraphTraits.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/Support/DOTGraphTraits.h"

#include "revng-c/RestructureCFG/GeneratorIterator.h"
#include "revng-c/RestructureCFG/MarkerCallUtils.h"

inline cppcoro::generator<llvm::BasicBlock *>
incrementGoto(llvm::BasicBlock *BB) {

  //  First of all, we return all the standard successors of `BB`
  for (auto *Successor : successors(BB)) {
    co_yield Successor;
  }

  // We then move to returning the additional successor represented by the
  // `ScopeCloser` edge, if present at all
  llvm::BasicBlock *GotoTarget = getGotoTarget(BB);
  if (GotoTarget) {
    co_yield GotoTarget;
  }

  // TODO: co_return end iterator
}

/// This class is used as a marker class to tell the graph iterator to treat the
/// underlying graph considering also the _goto_ edges needed for some
/// transformations
template<class GraphType>
struct Goto {
  const GraphType &Graph;

  inline Goto(const GraphType &G) : Graph(G) {}
};

/// Specializes `GraphTraits<Goto<T *>>`
template<>
struct llvm::GraphTraits<Goto<llvm::BasicBlock *>> {
public:
  using NodeRef = llvm::BasicBlock *;
  using ChildIteratorType = GeneratorIterator<llvm::BasicBlock *>;

public:
  static ChildIteratorType child_begin(NodeRef N) {
    return GeneratorIterator<llvm::BasicBlock *>(incrementGoto(N));
  }

  static ChildIteratorType child_end(NodeRef N) {
    return GeneratorIterator<llvm::BasicBlock *>();
  }

  // In the implementation for `llvm::BasicBlock *` trait we simply return
  // `this`
  static NodeRef getEntryNode(Goto<NodeRef> N) { return N.Graph; }

  // Add a verify method to the trait which checks that we have at maximum one
  // occurrence of the marker call in each `BasicBlock`, in the correct position
  static void verify(NodeRef N) { verifyBasicBlock("goto_target", N); }
};

template<>
struct llvm::GraphTraits<Goto<llvm::Function *>>
  : public llvm::GraphTraits<Goto<typename llvm::BasicBlock *>> {
  using NodeRef = llvm::BasicBlock *;
  using nodes_iterator = pointer_iterator<Function::iterator>;

  static NodeRef getEntryNode(Goto<llvm::Function *> G) {
    return &G.Graph->getEntryBlock();
  }

  static nodes_iterator nodes_begin(Goto<llvm::Function *> G) {
    return nodes_iterator(G.Graph->begin());
  }

  static nodes_iterator nodes_end(Goto<llvm::Function *> G) {
    return nodes_iterator(G.Graph->end());
  }

  static size_t size(Goto<llvm::Function *> G) { return G.Graph->size(); }

  // Add a verify method to the trait which invokes the `verify` of the
  // `BasicBlock *` trait for each node in the graph
  static void verify(Goto<llvm::Function *> G) {
    for (auto &N : *G.Graph) {
      llvm::GraphTraits<Goto<llvm::BasicBlock *>>::verify(&N);
    }
  }
};

template<>
struct llvm::DOTGraphTraits<Goto<llvm::Function *>>
  : public llvm::DefaultDOTGraphTraits {
  using llvm::DefaultDOTGraphTraits::DefaultDOTGraphTraits;

  std::string getNodeLabel(const llvm::BasicBlock *N,
                           const Goto<llvm::Function *> G) {
    return N->getName().str();
  }

  // TODO: we may want to specialize the `getEdgeAttributes` method in order to
  //       print the `GotoTarget` edges as dashed
};
