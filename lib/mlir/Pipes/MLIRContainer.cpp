//
// Copyright (c) rev.ng Labs Srl. See LICENSE.md for details.
//

#include <optional>

#include "mlir/Bytecode/BytecodeReader.h"
#include "mlir/Bytecode/BytecodeWriter.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/FunctionInterfaces.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Parser/Parser.h"

#include "revng/Pipeline/RegisterContainerFactory.h"

#include "revng-c/Pipes/Kinds.h"
#include "revng-c/mlir/Dialect/Clift/IR/Clift.h"
#include "revng-c/mlir/Pipes/MLIRContainer.h"

#include "MLIRLLVMHelpers.h"

using namespace revng;
using namespace revng::pipes;
using namespace revng::mlir_llvm;

static constexpr auto &TheKind = kinds::MLIRLLVMFunctionKind;

using ContextPtr = std::unique_ptr<mlir::MLIRContext>;
using OwningModuleRef = mlir::OwningOpRef<mlir::ModuleOp>;

static OwningModuleRef cloneModule(mlir::MLIRContext &Context,
                                   mlir::ModuleOp Module) {
  llvm::SmallString<1024> Buffer;
  llvm::raw_svector_ostream Out(Buffer);
  mlir::writeBytecodeToFile(Module, Out);

  mlir::Block OuterBlock;
  mlir::LogicalResult R = mlir::readBytecodeFile(llvm::MemoryBufferRef(Buffer,
                                                                       "BYTECOD"
                                                                       "E1"),
                                                 &OuterBlock,
                                                 mlir::ParserConfig(&Context));
  revng_assert(mlir::succeeded(R));

  auto &Operations = OuterBlock.getOperations();
  revng_assert(Operations.size() == 1);

  auto NewModule = mlir::cast<mlir::ModuleOp>(Operations.front());
  NewModule->remove();

  const size_t SrcOpsCount = getModuleOperations(Module).size();
  const size_t NewOpsCount = getModuleOperations(NewModule).size();
  revng_assert(NewOpsCount == SrcOpsCount);

  return NewModule;
}

static pipeline::Target makeTarget(const llvm::StringRef Name) {
  return pipeline::Target(Name, TheKind);
}

static llvm::StringRef getFunctionName(const pipeline::Target &Target) {
  revng_check(&Target.getKind() == &TheKind);
  revng_check(Target.getPathComponents().size() == 1);

  auto &&Name = Target.getPathComponents().front();
  static_assert(std::is_lvalue_reference_v<decltype(Name)>);
  return Name;
}

static void makeExternal(mlir::FunctionOpInterface F) {
  revng_assert(F->getNumRegions() == 1);
  // A function is made external by clearing its region.
  F->getRegion(0).getBlocks().clear();
}

static bool isExternal(mlir::Operation &O) {
  return mlir::cast<mlir::FunctionOpInterface>(O).isExternal();
}

static void pruneSymbols(mlir::ModuleOp Module) {
  llvm::DenseSet<mlir::Operation *> Symbols;

  Module->walk([&](mlir::SymbolOpInterface S) {
    if (auto F = mlir::dyn_cast<mlir::FunctionOpInterface>(S.getOperation())) {
      if (getFunctionName(F))
        return;
    }
    Symbols.insert(S);
  });

  Module->walk([&](mlir::FunctionOpInterface F) {
    if (F.isExternal())
      return;
    revng_assert(getFunctionName(F));

    if (const auto &Uses = mlir::SymbolTable::getSymbolUses(F)) {
      for (const mlir::SymbolTable::SymbolUse &Use : *Uses) {
        const auto &SymbolRef = Use.getSymbolRef();
        revng_assert(SymbolRef.getNestedReferences().empty());

        mlir::Operation *const
          Symbol = mlir::SymbolTable::lookupSymbolIn(Module,
                                                     SymbolRef
                                                       .getRootReference());
        revng_assert(Symbol);

        Symbols.erase(Symbol);
      }
    }
  });

  for (mlir::Operation *const O : Symbols)
    O->erase();
}

const char MLIRContainer::ID = 0;

MLIRContainer::MLIRContainer(const llvm::StringRef Name) :
  pipeline::Container<MLIRContainer>(Name) {
}

mlir::ModuleOp MLIRContainer::getOrCreateModule() {
  if (not Context) {
    revng_assert(not Module);
    Context = makeContext();
    Module = mlir::ModuleOp::create(mlir::UnknownLoc::get(Context.get()));
  }
  return *Module;
}

void MLIRContainer::setModule(mlir::OwningOpRef<mlir::ModuleOp> &&NewModule) {
  revng_assert(NewModule);

  // Make any non-target functions external.
  NewModule->walk([&](mlir::FunctionOpInterface F) {
    if (not getFunctionName(F))
      makeExternal(F);
  });

  // Prune
  pruneSymbols(*NewModule);

  setModuleInternal(std::move(NewModule));
}

void MLIRContainer::setModuleInternal(mlir::OwningOpRef<mlir::ModuleOp>
                                        &&NewModule) {
  revng_assert(NewModule);
  revng_assert(NewModule->getContext() == Context.get());

  Targets.clear();
  NewModule->walk([&](mlir::FunctionOpInterface F) {
    if (const auto Name = getFunctionName(F)) {
      const auto R = Targets.try_emplace(*Name, F);
      revng_assert(R.second);
    } else {
      // Non-target functions must be external.
      revng_assert(F.isExternal());
    }
  });

  Module = std::move(NewModule);
}

std::unique_ptr<pipeline::ContainerBase>
MLIRContainer::cloneFiltered(const pipeline::TargetsList &Filter) const {
  auto NewContainer = std::make_unique<MLIRContainer>(name());
  if (Context) {
    mlir::IRMapping Map;
    const auto NewRawModule = mlir::cast<mlir::ModuleOp>((*Module)->clone(Map));
    OwningModuleRef NewModule(NewRawModule);

    bool FilteredSome = false;
    for (auto [Name, F] : Targets) {
      if (F.isExternal())
        continue;

      if (not Filter.contains(makeTarget(Name))) {
        const auto NewFunction = Map.lookup(F.getOperation());
        makeExternal(mlir::cast<mlir::FunctionOpInterface>(NewFunction));
        FilteredSome = true;
      }
    }

    if (FilteredSome)
      pruneSymbols(*NewModule);

    NewContainer->Context = makeContext();
    NewContainer->Context->appendDialectRegistry(Context->getDialectRegistry());
    NewContainer->setModuleInternal(cloneModule(*NewContainer->Context,
                                                *NewModule));
  }
  return NewContainer;
}

void MLIRContainer::mergeBackImpl(MLIRContainer &&Container) {
  // This implementation of A.mergeBack(B) merges existing operations not
  // present in B from A into B. This is not necessarily very efficient compared
  // to merging operations from B into A, but it is the simpler implementation,
  // because for any operation present in both, the newest version is always
  // found in B.

  if (not Container.Context)
    return;

  if (Context) {
    using Iterator = decltype(Targets)::iterator;
    llvm::SmallVector<std::pair<Iterator, mlir::Operation *>, 16> NewTargets;

    for (const auto &[Name, Operation] : Targets) {
      const auto R = Container.Targets.try_emplace(Name, nullptr);

      if (R.second || isExternal(*R.first->second)) {
        NewTargets.emplace_back(R.first, Operation);
      } else {
        Operation->erase();
      }
    }

    if (not NewTargets.empty()) {
      pruneSymbols(*Module);

      OwningModuleRef NewModule = cloneModule(*Container.Context, *Module);
      for (auto const &[NewIterator, Operation] : NewTargets) {
        using ST = mlir::SymbolTable;
        mlir::Operation *const
          NewOperation = ST::lookupSymbolIn(*NewModule,
                                            ST::getSymbolName(Operation));
        revng_assert(NewOperation);

        if (NewIterator->second)
          NewIterator->second->erase();

        NewOperation->remove();
        getModuleBlock(*Container.Module).push_back(NewOperation);
      }
    }
  }

  Targets = std::move(Container.Targets);
  Module = std::move(Container.Module);
  Context = std::move(Container.Context);
}

pipeline::TargetsList MLIRContainer::enumerate() const {
  pipeline::TargetsList::List List;

  if (Context) {
    List.reserve(Targets.size());
    for (const auto &[Name, O] : Targets)
      List.push_back(makeTarget(Name));
    llvm::sort(List);
  }

  return pipeline::TargetsList(std::move(List));
}

bool MLIRContainer::remove(const pipeline::TargetsList &List) {
  bool RemovedSome = false;
  for (const pipeline::Target &T : List) {
    const auto I = Targets.find(getFunctionName(T));

    if (I == Targets.end())
      continue;

    if (I->second.isExternal())
      continue;

    makeExternal(I->second);
    RemovedSome = true;
  }

  if (RemovedSome) {
    // If any functions were removed, prune symbols and garbage collect types by
    // cloning the module into a new context.

    pruneSymbols(*Module);

    auto NewContext = makeContext();
    auto NewModule = cloneModule(*NewContext, *Module);

    // Delete the existing module before replacing the context.
    Module = nullptr;

    Context = std::move(NewContext);
    setModuleInternal(std::move(NewModule));
  }

  return RemovedSome;
}

void MLIRContainer::clear() {
  Targets.clear();
  Module = {};
  Context = {};
}

llvm::Error MLIRContainer::serialize(llvm::raw_ostream &OS) const {
  if (Context)
    (*Module).print(OS, mlir::OpPrintingFlags().enableDebugInfo());
  return llvm::Error::success();
}

llvm::Error MLIRContainer::deserialize(const llvm::MemoryBuffer &Buffer) {
  revng_assert(not Context);
  auto NewContext = makeContext();

  const mlir::ParserConfig Config(NewContext.get());
  OwningModuleRef
    NewModule = mlir::parseSourceString<mlir::ModuleOp>(Buffer.getBuffer(),
                                                        Config);

  if (not NewModule)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "Cannot load MLIR/LLVM module.");

  Context = std::move(NewContext);
  setModuleInternal(std::move(NewModule));

  return llvm::Error::success();
}

llvm::Error MLIRContainer::extractOne(llvm::raw_ostream &OS,
                                      const pipeline::Target &Target) const {
  return cloneFiltered(pipeline::TargetsList::List{ Target })->serialize(OS);
}

std::vector<pipeline::Kind *> MLIRContainer::possibleKinds() {
  return { &TheKind };
}

std::unique_ptr<mlir::MLIRContext> MLIRContainer::makeContext() {
  const auto Threading = mlir::MLIRContext::Threading::DISABLED;
  return std::make_unique<mlir::MLIRContext>(Threading);
}

static pipeline::RegisterDefaultConstructibleContainer<MLIRContainer> X;
