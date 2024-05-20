//
// Copyright rev.ng Labs Srl. See LICENSE.md for details.
//

#include "llvm/IR/IRBuilder.h"

#include "revng/Support/Assert.h"

#include "revng-c/RestructureCFG/BundleEdgesByCycle.h"
#include "revng-c/RestructureCFG/CycleEquivalenceAnalysis.h"
#include "revng-c/RestructureCFG/CycleEquivalencePass.h"

char BundleEdgesByCyclePass::ID = 0;

static constexpr const char *Flag = "bundle-edges-by-cycle";
using Reg = llvm::RegisterPass<BundleEdgesByCyclePass>;
static Reg X(Flag, "Bundle Edges by Cycle Equivalence Class ID");

using namespace llvm;

class BundleEdgesByCycleImpl {
private:
  // This `enum class` is used to specialize the code in the `connectDispatcher`
  // helper function
  enum class DispatcherTy {
    IncomingDispatcher,
    OutgoingDispatcher,
  };

public:
  using CER = CycleEquivalenceAnalysis<Function *>::CycleEquivalenceResult;

private:
  CER &CycleEquivalenceResult;

public:
  BundleEdgesByCycleImpl(CER &CycleEquivalenceResult) :
    CycleEquivalenceResult(CycleEquivalenceResult) {}

  void run(llvm::Function &F) {
    // We save the postorder, since we insert new blocks during the
    // transformation, and we may invalidate the `llvm::post_order`
    llvm::SmallVector<BasicBlock *> InitialPostOrder;
    for (BasicBlock *BB : post_order(&F)) {
      InitialPostOrder.push_back(BB);
    }

    // We iterate over the `BasicBlock`s in the function according
    for (BasicBlock *BB : InitialPostOrder) {

      // For each node, we divide the predecessor edges, if they belong to
      // different `Cycle Equivalence Class ID`s, so that they are grouped into
      // different entry nodes
      std::map<size_t, BasicBlock *> CECIDToBlockPredecessorMap;

      llvm::SmallVector<BasicBlock *> Predecessors;
      for (BasicBlock *Predecessor : predecessors(BB)) {
        Predecessors.push_back(Predecessor);
      }

      for (BasicBlock *Predecessor : Predecessors) {

        // In order to retrieve the `Cycle Equivalence Class ID`, we need the
        // know the successor index (the index of the edge we are moving,
        // relative to its source node). For this reason, we need to perform the
        // moving of the edge with an exploration which starts from each
        // `Predecessor` of the `BB` node.
        llvm::SmallVector<BasicBlock *> Successors;
        for (BasicBlock *Successor : successors(Predecessor)) {
          Successors.push_back(Successor);
        }

        for (const auto &[Index, Successor] : llvm::enumerate(Successors)) {

          // We are only interested in moving edges reaching the `BB` block in
          // each iteration. The complete iteration is used and necessary just
          // in order to keep track of the `Index` value (which is an integral
          // part of how we query the `CycleEquivalenceResult` mapping).
          if (Successor != BB) {
            continue;
          }

          using DispatcherTy::IncomingDispatcher;
          connectDispatcher<IncomingDispatcher>(Predecessor,
                                                BB,
                                                Index,
                                                CECIDToBlockPredecessorMap);
        }
      }

      // For each node, we divide the successor edges, if they belong to
      // different `Cycle Equivalence Class ID`s, so that they are grouped into
      // different exit nodes
      std::map<size_t, BasicBlock *> CECIDToBlockSuccessorMap;

      llvm::SmallVector<BasicBlock *> Successors;
      for (BasicBlock *Successor : successors(BB)) {
        Successors.push_back(Successor);
      }

      for (const auto &[Index, Successor] : llvm::enumerate(Successors)) {
        using DispatcherTy::OutgoingDispatcher;
        connectDispatcher<OutgoingDispatcher>(BB,
                                              Successor,
                                              Index,
                                              CECIDToBlockSuccessorMap);
      }
    }
  }

  /// This helper function is responsible of inserting a new `Dispatcher` block
  /// by splitting the edge between the `Source` block (with the n-th successor
  /// edge identified with the `Index` parameter) and the `Target` block, with
  /// the goal of grouping the edges by `Cycle Equivalence Class ID`
  template<DispatcherTy DispatcherType>
  void connectDispatcher(BasicBlock *Source,
                         BasicBlock *Target,
                         size_t Index,
                         std::map<size_t, BasicBlock *>
                           &CycleEquivalenceClassIDToBlockEdgeMap) {
    using CER = CycleEquivalenceAnalysis<Function *>::CycleEquivalenceResult;
    using EdgeDescriptor = CER::EdgeDescriptor;

    BasicBlock *Dispatcher = nullptr;

    // The successor `Index` is fundamental in order to query the
    // `CycleEquivalenceResult` map, in order to disambiguate between edges
    // insisting between the same node pair
    auto Edge = EdgeDescriptor({ Source, Target, Index });
    size_t EdgeID = CycleEquivalenceResult.getCycleEquivalenceClassID(Edge);

    // We verify if we already have a dispatcher block for a certain `cycle
    // equivalence class ID`, and, if not, in case we materialize one
    auto It = CycleEquivalenceClassIDToBlockEdgeMap.find(EdgeID);
    if (It != CycleEquivalenceClassIDToBlockEdgeMap.end()) {
      Dispatcher = It->second;

      if (DispatcherType == DispatcherTy::OutgoingDispatcher) {

        // When using an already created `OutgoingDispatcher`, we assert that
        // the unique successor it has been connected to when the `Dispatcher`
        // was created, is the same block target of the outgoing edge we are
        // currently redirecting to it, in order to preserve the original
        // semantics of the control flow. By construction `OutgoingDispatcher`
        // has a unique successor. A violation of this condition does not mean
        // that it is impossible to perform this transformation (we would need
        // to additionally group the outgoing edges both by `Cycle Equivalence
        // Class ID` and reached successors), but due to our understanding of
        // cycle equivalence, this should never happen.
        auto *OriginalSuccessor = Dispatcher->getUniqueSuccessor();
        revng_assert(OriginalSuccessor == Target);
      }
    } else {

      // We create a new dedicated dispatcher block for grouping all the
      // edges belonging to a specific `Cycle Equivalence Class ID`. We add
      // to the block name as a suffix the `Cycle Equivalence Class ID`.
      size_t ClassIndex = EdgeID;

      // Depending on whether we are inserting an incoming or outgoing
      // dispatcher block, we specialize the code
      if constexpr (DispatcherType == DispatcherTy::IncomingDispatcher) {
        Dispatcher = BasicBlock::Create(Target->getContext(),
                                        Target->getName() + "_pred_ceci_"
                                          + std::to_string(ClassIndex),
                                        Target->getParent());
      } else if (DispatcherType == DispatcherTy::OutgoingDispatcher) {
        Dispatcher = BasicBlock::Create(Source->getContext(),
                                        Source->getName() + "_succ_ceci_"
                                          + std::to_string(ClassIndex),
                                        Source->getParent());
      } else {
        revng_abort();
      }

      // We connect the new dispatcher `BasicBlock` to the `Target` block
      IRBuilder<> Builder(Source->getParent()->getContext());
      Builder.SetInsertPoint(Dispatcher);
      Builder.CreateBr(Target);

      // Assign the `Cycle Equivalence Class ID` for the new edge.
      // `SuccNum` is by construction 0 (the inserted edge is the first and
      // only successor edge for the `IncomingDispatcher` block).
      CycleEquivalenceResult.insert(EdgeDescriptor(Dispatcher, Target, 0),
                                    EdgeID);

      // Populate the `CycleEquivalenceClassID` to the `Dispatcher`
      // mapping
      CycleEquivalenceClassIDToBlockEdgeMap[EdgeID] = Dispatcher;
    }

    // Connect the `Source` block to the `Dispatcher` block instead of the
    // `Target` block. We explicitly keep track of the edge `Index` of the edge
    // we are splitting during the outermost iteration, so we can use it to
    // populate the `CycleEquivalenceResult` mapping.
    llvm::Instruction *Terminator = Source->getTerminator();
    Terminator->setSuccessor(Index, Dispatcher);
    CycleEquivalenceResult.insert(EdgeDescriptor(Source, Dispatcher, Index),
                                  EdgeID);
  }
};

bool BundleEdgesByCyclePass::runOnFunction(llvm::Function &F) {

  // We need to obtain a copy of the `CycleEquivalenceResult`, because in this
  // pass we need to insert the mapping for the additional edges that we
  // introduce during the transformation
  using CER = BundleEdgesByCycleImpl::CER;
  CER CycleEquivalenceResult = getAnalysis<CycleEquivalencePass>().getResult();

  BundleEdgesByCycleImpl Impl(CycleEquivalenceResult);
  Impl.run(F);

  // This pass modifies the module, adding predecessors and successors to the
  // nodes
  return true;
}

void BundleEdgesByCyclePass::getAnalysisUsage(llvm::AnalysisUsage &AU) const {

  // This pass depends on the `Cycle Equivalence` analysis
  AU.addRequired<CycleEquivalencePass>();
}
