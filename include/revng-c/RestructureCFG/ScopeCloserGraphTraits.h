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
incrementScope(llvm::BasicBlock *BB) {

  //  First of all, we return all the standard successors of `BB`
  for (auto *Successor : successors(BB)) {
    co_yield Successor;
  }

  // We then move to returning the additional successor represented by the
  // `ScopeCloser` edge, if present at all
  llvm::BasicBlock *ScopeCloserTarget = getScopeCloser(BB);
  if (ScopeCloserTarget) {
    co_yield ScopeCloserTarget;
  }

  // TODO: co_return end iterator
}

/// This class is used as a marker class to tell the graph iterator to treat the
/// underlying graph considering also the scope closer edges needed for, e.g.,
/// IDB
template<class GraphType>
struct Scope {
  const GraphType &Graph;

  inline Scope(const GraphType &G) : Graph(G) {}
};

/// Specializes `GraphTraits<Scope<llvm::BasicBlock *>>`
template<>
struct llvm::GraphTraits<Scope<llvm::BasicBlock *>> {
public:
  using NodeRef = llvm::BasicBlock *;
  using ChildIteratorType = GeneratorIterator<llvm::BasicBlock *>;

public:
  static ChildIteratorType child_begin(NodeRef N) {
    return GeneratorIterator<llvm::BasicBlock *>(incrementScope(N));
  }

  static ChildIteratorType child_end(NodeRef N) {
    return GeneratorIterator<llvm::BasicBlock *>();
  }

  // In the implementation for `llvm::BasicBlock *` trait we simply return
  // `this`
  static NodeRef getEntryNode(Scope<NodeRef> N) { return N.Graph; }

  // Add a verify method to the trait which checks that we have at maximum one
  // occurrence of the marker call in each `BasicBlock`, in the correct position
  static void verify(NodeRef N) { verifyBasicBlock("scope_closer", N); }
};

template<>
struct llvm::GraphTraits<Scope<llvm::Function *>>
  : public llvm::GraphTraits<Scope<typename llvm::BasicBlock *>> {
  using NodeRef = llvm::BasicBlock *;
  using nodes_iterator = pointer_iterator<Function::iterator>;

  static NodeRef getEntryNode(Scope<llvm::Function *> G) {
    return &G.Graph->getEntryBlock();
  }

  static nodes_iterator nodes_begin(Scope<llvm::Function *> G) {
    return nodes_iterator(G.Graph->begin());
  }

  static nodes_iterator nodes_end(Scope<llvm::Function *> G) {
    return nodes_iterator(G.Graph->end());
  }

  static size_t size(Scope<llvm::Function *> G) { return G.Graph->size(); }

  // Add a verify method to the trait which invokes the `verify` of the
  // `BasicBlock *` trait for each node in the graph
  static void verify(Scope<llvm::Function *> G) {
    for (auto &N : *G.Graph) {
      llvm::GraphTraits<Scope<llvm::BasicBlock *>>::verify(&N);
    }
  }
};

template<>
struct llvm::DOTGraphTraits<Scope<llvm::Function *>>
  : public llvm::DefaultDOTGraphTraits {
  using llvm::DefaultDOTGraphTraits::DefaultDOTGraphTraits;

  std::string getNodeLabel(const llvm::BasicBlock *N,
                           const Scope<llvm::Function *> G) {
    return N->getName().str();
  }

  // TODO: we may want to specialize `getEdgeAttributes` method in order to
  //       print the `ScopeGraph` edges as dashed
};
