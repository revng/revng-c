//
// Copyright rev.ng Labs Srl. See LICENSE.md for details.
//

#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/Support/GraphWriter.h"

#include "revng/Support/IRHelpers.h"

#include "revng-c/RestructureCFG/GotoGraphLoggerPass.h"
#include "revng-c/RestructureCFG/GotoTargetGraphTraits.h"

using namespace llvm;

// Debug logger
Logger<> GotoGraphLoggerPassLogger("goto-graph-logger");

class GotoGraphLoggerPassImpl {
  llvm::Function &F;

public:
  GotoGraphLoggerPassImpl(llvm::Function &F) : F(F) {}

public:
  bool run() {
    revng_log(GotoGraphLoggerPassLogger,
              "GotoGraph of function: " << F.getName().str() << "\n");
    for (llvm::BasicBlock &BB : F) {
      revng_log(GotoGraphLoggerPassLogger,
                "Block " << BB.getName().str() << " successors:\n");
      using GotoGraph = llvm::GraphTraits<Goto<llvm::BasicBlock *>>;
      for (auto Succ = GotoGraph::child_begin(&BB);
           Succ != GotoGraph::child_end(&BB);
           ++Succ) {
        revng_log(GotoGraphLoggerPassLogger,
                  "  " << (*Succ)->getName().str() << "\n");
      }
    }

    return false; // The function was not modified
  }
};

char GotoGraphLoggerPass::ID = 0;

static constexpr const char *Flag = "goto-graph-logger";
using Reg = llvm::RegisterPass<GotoGraphLoggerPass>;
static Reg X(Flag, "Dump edge information on the `GotoGraph`");

bool GotoGraphLoggerPass::runOnFunction(llvm::Function &F) {

  // Instantiate and call the `Impl` class
  GotoGraphLoggerPassImpl GGLPImpl(F);
  return GGLPImpl.run();
}

void GotoGraphLoggerPass::getAnalysisUsage(llvm::AnalysisUsage &AU) const {

  // This is a read only analysis, that does not touch the IR
  AU.setPreservesAll();
}
