//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Pass.h"

#include "revng/Model/IRHelpers.h"
#include "revng/Model/LoadModelPass.h"
#include "revng/Pipeline/Location.h"
#include "revng/Pipes/Ranks.h"
#include "revng/Yield/ControlFlow/Comments.h"

#include "revng-c/Support/DecompilationHelpers.h"

static std::optional<MetaAddress>
tryExtractAddress(const llvm::Instruction &I) {
  if (!I.getDebugLoc() || !I.getDebugLoc()->getScope())
    return std::nullopt;

  auto DebugLocation = I.getDebugLoc()->getScope()->getName().str();
  auto Parsed = pipeline::locationFromString(revng::ranks::Instruction,
                                             DebugLocation);
  revng_assert(Parsed.has_value());

  MetaAddress Extracted = Parsed->at(revng::ranks::Instruction);
  revng_assert(Extracted.isValid());
  return Extracted;
}

template<typename ResultSet>
static ResultSet gatherNonStatementAddresses(const llvm::Instruction &I,
                                             ResultSet &&Result = {}) {
  if (std::optional MaybeAddress = tryExtractAddress(I))
    Result.emplace(*MaybeAddress);

  for (const llvm::Use &V : I.operands())
    if (const llvm::Instruction *Cast = llvm::dyn_cast<llvm::Instruction>(V))
      if (not isStatement(*Cast))
        gatherNonStatementAddresses(*Cast, Result);

  return Result;
}

using DTBasicBlockNode = llvm::DomTreeNodeOnView<llvm::BasicBlock,
                                                 llvm::DTIdentityView>;

template<>
struct yield::StatementTraits<const DTBasicBlockNode *> {

  using StatementType = const llvm::Instruction *;
  using AddressType = const MetaAddress;
  using LocationType = const std::set<MetaAddress>;

  static auto getStatements(const DTBasicBlockNode *Node) {
    return *Node->getBlock() | std::views::filter(isStatement)
           | std::views::transform([](auto &&R) { return &R; });
  }

  static LocationType getAddresses(StatementType Statement) {
    return gatherNonStatementAddresses<std::decay_t<LocationType>>(*Statement);
  }
};

static std::string
addressesToString(range_with_value_type<MetaAddress> auto const &Addresses) {
  std::string Result = "";

  if (not Addresses.empty()) {
    for (const MetaAddress &Address : Addresses)
      Result += Address.toString() + " + ";
    Result.resize(Result.size() - 3);
  }

  return Result;
}

struct EmbedContextualComments : public llvm::FunctionPass {
public:
  static char ID;

  EmbedContextualComments() : llvm::FunctionPass(ID) {}

  bool runOnFunction(llvm::Function &F) override {
    auto &ModelWrapper = getAnalysis<LoadModelWrapperPass>().get();
    const TupleTree<model::Binary> &Model = ModelWrapper.getReadOnlyModel();

    auto ModelFunction = llvmToModelFunction(*Model, F);
    revng_assert(ModelFunction != nullptr);

    if (ModelFunction->Comments().empty())
      return false;

    bool WasModified = false;

    llvm::DominatorTree DT(F);
    using MapT = yield::ContextualCommentMap<const DTBasicBlockNode *>;
    MapT CM(*ModelFunction, DT.getRootNode());

    llvm::Module &M = *F.getParent();
    using StringLiteral = const uint8_t *;
    llvm::FunctionType &FT = *createFunctionType<void,
                                                 int64_t,
                                                 bool,
                                                 StringLiteral,
                                                 StringLiteral>(M.getContext());
    auto IRComment = M.getOrInsertFunction("comment", &FT);
    auto &Callee = *llvm::cast<llvm::Function>(IRComment.getCallee());
    Callee.addFnAttr(llvm::Attribute::NoUnwind);
    Callee.addFnAttr(llvm::Attribute::WillReturn);
    Callee.addFnAttr(llvm::Attribute::NoMerge);
    Callee.setDoesNotAccessMemory();
    FunctionTags::Comment.addTo(&Callee);

    llvm::IRBuilder<> B(M.getContext());
    auto EmitAComment = [&B, &M, &IRComment](llvm::Instruction *Where,
                                             const MapT::CommentView &Comment,
                                             llvm::StringRef EmittedLocation) {
      B.SetInsertPoint(Where);

      std::array<llvm::Value *, 4> Arguments = {
        llvm::ConstantInt::get(llvm::Type::getInt64Ty(B.getContext()),
                               Comment.CommentIndex),
        llvm::ConstantInt::get(llvm::Type::getInt8Ty(B.getContext()),
                               Comment.LocationMatchesExactly),
        getUniqueString(&M, addressesToString(*Comment.ExpectedLocation)),
        getUniqueString(&M, EmittedLocation)
      };

      auto *Call = B.CreateCall(IRComment, Arguments);
      Call->copyMetadata(*Where);
    };

    llvm::SmallVector<llvm::Value *, 8> Argumentss;
    for (DTBasicBlockNode *Node : llvm::depth_first(DT.getRootNode())) {
      using Trait = yield::StatementTraits<const DTBasicBlockNode *>;
      for (llvm::Instruction *I : Trait::getStatements(Node)) {
        for (const MapT::CommentView &Comment : CM.getComments(I)) {
          EmitAComment(I, Comment, addressesToString(Trait::getAddresses(I)));

          WasModified = true;
        }
      }
    }

    for (const MapT::CommentView &Comment :
         std::views::reverse(CM.getHomelessComments())) {
      // For now emit homeless comments at the very top of the function.
      // TODO: find a better place for them.
      EmitAComment(&*F.begin()->begin(),
                   Comment,
                   "at the function entry point");

      WasModified = true;
    }

    return WasModified;
  }

  void getAnalysisUsage(llvm::AnalysisUsage &AU) const override {
    AU.addRequired<LoadModelWrapperPass>();
    AU.setPreservesCFG();
  }
};

char EmbedContextualComments::ID = 0;

using ECC = EmbedContextualComments;
static llvm::RegisterPass<ECC> R("embed-contextual-comments",
                                 "This add the contextual comment information "
                                 "from the model.",
                                 false,
                                 false);
