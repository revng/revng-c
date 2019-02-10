//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#ifndef REVNGC_BASICBLOCKVIEWANALYSIS_H
#define REVNGC_BASICBLOCKVIEWANALYSIS_H

// LLVM includes
#include <llvm/IR/Function.h>

// revng includes
#include <revng/Support/MonotoneFramework.h>

// revng-c includes
#include "revng-c/RestructureCFGPass/BasicBlockNode.h"
#include "revng-c/RestructureCFGPass/RegionCFGTree.h"

class BasicBlockNode;

namespace BasicBlockViewAnalysis {

using BBMap = std::map<llvm::BasicBlock *, llvm::BasicBlock *>;
using BBViewMap = std::map<llvm::BasicBlock *, BBMap>;

using BBNodeToBBMap = std::map<BasicBlockNode *, llvm::BasicBlock *>;

class BasicBlockViewMap {

public:
  using iterator = BBMap::iterator;
  using const_iterator = BBMap::const_iterator;
  using value_type = BBMap::value_type;
  using key_type = BBMap::key_type;
  using mapped_type = BBMap::mapped_type;

protected:
  BBMap Map;

protected:
  BasicBlockViewMap(const BasicBlockViewMap &) = default;

public:
  BasicBlockViewMap() : Map() {}

  BasicBlockViewMap copy() const { return *this; }
  BasicBlockViewMap &operator=(const BasicBlockViewMap &) = default;

  BasicBlockViewMap(BasicBlockViewMap &&) = default;
  BasicBlockViewMap &operator=(BasicBlockViewMap &&) = default;

  BBMap copyMap() const { return Map; }

  static BasicBlockViewMap bottom() { return BasicBlockViewMap(); }

public:
  bool lowerThanOrEqual(const BasicBlockViewMap &RHS) const {
    if (Map.size() > RHS.Map.size())
      return false; // this is larger than RHS, so it cannot be smaller

    for (const value_type &ThisView : Map) {
      auto RHSIt = RHS.Map.find(ThisView.first);
      if (RHSIt == RHS.Map.end()) {
        // Something is in this->Map but is not in RHS.Map, so this is not
        // lowerThanOrEqual than RHS
        return false;
      } else /* RHSIt != Map.end() */ {
        // If the mapped values are the same it's ok.
        // If in RHS the mapped value is nullptr is also ok.
        // In all the other cases, RHS and this disagree, hence return false
        if (not (RHSIt->second == ThisView.second or RHSIt->second == nullptr))
          return false;
      }
    }

    return true;
  }

  void combine(const BasicBlockViewMap &RHS);

public: // map methods
  iterator begin() { return Map.begin(); }
  iterator end() { return Map.end(); }
  const_iterator begin() const { return Map.cbegin(); }
  const_iterator end() const { return Map.cend(); }
  std::pair<iterator, bool> insert (const value_type &V) {
    return Map.insert(V);
  }
  std::pair<iterator, bool> insert (value_type &&V) {
    return Map.insert(V);
  }
  mapped_type &operator[](const key_type& Key) {
    return Map[Key];
  }
  mapped_type &operator[](key_type&& Key) {
    return Map[Key];
  }
  mapped_type &at(const key_type& Key) {
    return Map.at(Key);
  }
  const mapped_type &at(const key_type& Key) const {
    return Map.at(Key);
  }
};


class Analysis
  : public MonotoneFramework<Analysis,
                             BasicBlockNode *,
                             BasicBlockViewMap,
                             VisitType::PostOrder,
                             llvm::SmallVector<BasicBlockNode *, 2>> {
private:
  RegionCFG &RegionCFGTree;
  const BBNodeToBBMap &EnforcedBBMap;
  BBViewMap ViewMap;

public:
  using Base = MonotoneFramework<Analysis,
                                 BasicBlockNode *,
                                 BasicBlockViewMap,
                                 VisitType::PostOrder,
                                 llvm::SmallVector<BasicBlockNode *, 2>>;

  Analysis(RegionCFG &RegionCFGTree, const BBNodeToBBMap &EnforcedBBMap) :
    Base(&RegionCFGTree.getEntryNode()),
    RegionCFGTree(RegionCFGTree),
    EnforcedBBMap(EnforcedBBMap) {
      for (BasicBlockNode *BB : RegionCFGTree) {
        if (BB->successor_size() == 0)
          Base::registerExtremal(BB);
    }
  }

  void initialize() {
    Base::initialize();
    ViewMap.clear();
  }

  void assertLowerThanOrEqual(const BasicBlockViewMap &A,
                              const BasicBlockViewMap &B) const {
    revng_assert(A.lowerThanOrEqual(B));
  }

  const BBViewMap &getBBViewMap() const { return ViewMap; }

  BBViewMap &getBBViewMap() { return ViewMap; }

  /// This Analysis uses DefaultInterrupt, hence it is never supposed to dump
  /// the final state.
  void dumpFinalState() const { revng_abort(); }

  /// Gets the predecessor BasicBlockNode in the RegionCFGTree.
  /// Being a backward analysis the 'successors' in analysis order are the
  /// 'predecessor' in RegionCFG order.
  llvm::SmallVector<BasicBlockNode *, 2>
  successors(BasicBlockNode *BB, InterruptType &) const {
    llvm::SmallVector<BasicBlockNode *, 2> Result;
    for (BasicBlockNode *Pred : BB->predecessors())
      Result.push_back(Pred);
    return Result;
  }

  size_t successor_size(BasicBlockNode *BB, InterruptType &) const {
    return BB->predecessor_size();
  }

  static BasicBlockViewMap extremalValue(BasicBlockNode *) {
    return BasicBlockViewMap::bottom();
  }

  // ---- Transfer function and handleEdge, to propagate the analysis ----

  InterruptType transfer(BasicBlockNode *BB);

  llvm::Optional<BasicBlockViewMap>
  handleEdge(const BasicBlockViewMap &Original,
             BasicBlockNode *Source,
             BasicBlockNode *Destination) const {
    return llvm::Optional<BasicBlockViewMap>();
  };

};

} // end namespace BasicBlockViewAnalysis

#endif // REVNGC_BASICBLOCKVIEWANALYSIS_H
