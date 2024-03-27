#pragma once

//
// Copyright (c) rev.ng Labs Srl. See LICENSE.md for details.
//

#include <optional>

#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/Types.h"

#include "revng/Model/Binary.h"

#include "revng-c/mlir/Dialect/Clift/IR/Clift.h"

namespace revng {

mlir::clift::ValueType getUnqualifiedType(mlir::MLIRContext &Context,
                                          const model::Type &ModelType);

mlir::clift::ValueType
getUnqualifiedTypeChecked(llvm::function_ref<mlir::InFlightDiagnostic()>
                            EmitError,
                          mlir::MLIRContext &Context,
                          const model::Type &ModelType);

mlir::clift::ValueType getQualifiedType(mlir::MLIRContext &Context,
                                        const model::QualifiedType &ModelType);

mlir::clift::ValueType
getQualifiedTypeChecked(llvm::function_ref<mlir::InFlightDiagnostic()>
                          EmitError,
                        mlir::MLIRContext &Context,
                        const model::QualifiedType &ModelType);

} // namespace revng
