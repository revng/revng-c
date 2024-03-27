#pragma once

//
// Copyright (c) rev.ng Labs Srl. See LICENSE.md for details.
//

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"

namespace revng::mlir_llvm {

using LocType = mlir::FusedLocWith<mlir::LLVM::DISubprogramAttr>;

inline mlir::Block &getModuleBlock(mlir::ModuleOp Module) {
  revng_assert(Module);
  revng_assert(Module->getNumRegions() == 1);
  return Module->getRegion(0).front();
}

inline decltype(auto) getModuleOperations(mlir::ModuleOp Module) {
  return getModuleBlock(Module).getOperations();
}

inline std::optional<llvm::StringRef> getLinkageName(mlir::Operation *const O) {
  if (const auto L = mlir::dyn_cast<LocType>(O->getLoc()))
    return L.getMetadata().getLinkageName();
  return std::nullopt;
}

inline std::optional<llvm::StringRef>
getFunctionName(mlir::FunctionOpInterface F) {
  if (const auto Name = getLinkageName(F)) {
    static constexpr llvm::StringRef Path = "/function/";
    revng_assert(Name->starts_with(Path));
    return Name->substr(Path.size());
  }
  return std::nullopt;
}

} // namespace revng::mlir_llvm
