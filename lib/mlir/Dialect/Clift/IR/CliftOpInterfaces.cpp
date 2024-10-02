/// \file CliftOpInterfaces.cpp
/// Tests for the Clift Dialect

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//
//
#include "revng-c/mlir/Dialect/Clift/IR/CliftOpInterfaces.h"
#include "revng-c/mlir/Dialect/Clift/IR/CliftOps.h"
//
#include "revng-c/mlir/Dialect/Clift/IR/CliftOpInterfaces.cpp.inc"

bool mlir::clift::isLvalueExpression(mlir::Value Value) {
  mlir::Operation *Op = Value.getDefiningOp();

  if (auto ExprOp = mlir::dyn_cast<ExpressionOpInterface>(Value
                                                            .getDefiningOp()))
    return ExprOp.isLvalueExpression();

  return mlir::isa<LocalVariableOp, GlobalVariableOp>(Op);
}
