#pragma once

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include "revng-c/mlir/Dialect/Clift/IR/CliftAttributes.h"
#include "revng-c/mlir/Dialect/Clift/IR/CliftTypes.h"

namespace mlir::clift {

struct TypedefDecomposition {
  ValueType Type;
  bool HasConstTypedef;
};

/// Recursively decomposes a typedef into its underlying non-typedef type and a
/// boolean indicating whether any of the typedefs added const. Note that the
/// underlying type itself may also be const while the boolean may be false.
inline TypedefDecomposition decomposeTypedef(ValueType Type) {
  bool HasConstTypedef = false;

  while (true) {
    auto D = mlir::dyn_cast<DefinedType>(Type);
    if (not D)
      break;

    auto T = mlir::dyn_cast<TypedefTypeAttr>(D.getElementType());
    if (not T)
      break;

    Type = T.getUnderlyingType();
    HasConstTypedef |= D.isConst();
  }

  return { Type, HasConstTypedef };
}

inline ValueType dealias(ValueType Type) {
  auto [UnderlyingType, HasConstTypedef] = decomposeTypedef(Type);

  if (HasConstTypedef)
    UnderlyingType = UnderlyingType.addConst();

  return UnderlyingType;
}

inline bool isVoid(ValueType Type) {
  Type = dealias(Type);

  if (auto T = mlir::dyn_cast<PrimitiveType>(Type))
    return T.getKind() == PrimitiveKind::VoidKind;

  return false;
}

inline bool isCompleteType(ValueType Type) {
  Type = dealias(Type);

  if (auto T = mlir::dyn_cast<DefinedType>(Type)) {
    auto Definition = T.getElementType();
    if (auto D = mlir::dyn_cast<StructTypeAttr>(Definition))
      return D.isDefinition();
    if (auto D = mlir::dyn_cast<UnionTypeAttr>(Definition))
      return D.isDefinition();
    return true;
  }

  if (auto T = mlir::dyn_cast<ScalarTupleType>(Type))
    return T.isComplete();

  if (auto T = mlir::dyn_cast<ArrayType>(Type))
    return isCompleteType(T.getElementType());

  return true;
}

inline bool isScalarType(ValueType Type) {
  Type = dealias(Type);

  if (auto T = mlir::dyn_cast<PrimitiveType>(Type))
    return T.getKind() != PrimitiveKind::VoidKind;

  if (auto T = mlir::dyn_cast<DefinedType>(Type))
    return mlir::isa<EnumTypeAttr>(T.getElementType());

  return mlir::isa<PointerType>(Type);
}

inline bool isIntegerKind(PrimitiveKind Kind) {
  switch (Kind) {
  case PrimitiveKind::GenericKind:
  case PrimitiveKind::PointerOrNumberKind:
  case PrimitiveKind::NumberKind:
  case PrimitiveKind::UnsignedKind:
  case PrimitiveKind::SignedKind:
    return true;

  case PrimitiveKind::VoidKind:
  case PrimitiveKind::FloatKind:
    break;
  }
  return false;
}

inline PrimitiveType getUnderlyingIntegerType(ValueType Type) {
  Type = dealias(Type);

  if (auto T = mlir::dyn_cast<PrimitiveType>(Type))
    return isIntegerKind(T.getKind()) ? T : nullptr;

  if (auto T = mlir::dyn_cast<DefinedType>(Type)) {
    if (auto EnumT = mlir::dyn_cast<EnumTypeAttr>(T.getElementType()))
      return mlir::cast<PrimitiveType>(dealias(EnumT.getUnderlyingType()));
  }

  return nullptr;
}

inline bool isIntegerType(ValueType Type) {
  Type = dealias(Type);

  if (auto T = mlir::dyn_cast<PrimitiveType>(Type))
    return isIntegerKind(T.getKind());

  if (auto T = mlir::dyn_cast<DefinedType>(Type))
    return mlir::isa<EnumTypeAttr>(T.getElementType());

  return false;
}

inline bool isPointerType(ValueType Type) {
  return mlir::isa<PointerType>(dealias(Type));
}

inline bool isObjectType(ValueType Type) {
  Type = dealias(Type);

  if (auto T = mlir::dyn_cast<PrimitiveType>(Type)) {
    if (T.getKind() == PrimitiveKind::VoidKind)
      return false;
  }

  if (auto T = mlir::dyn_cast<DefinedType>(Type)) {
    if (mlir::isa<FunctionTypeAttr>(T.getElementType()))
      return false;
  }

  if (mlir::isa<ScalarTupleType>(Type))
    return false;

  return true;
}

inline bool isArrayType(ValueType Type) {
  return mlir::isa<ArrayType>(dealias(Type));
}

inline bool isClassType(ValueType Type) {
  if (auto T = mlir::dyn_cast<DefinedType>(Type)) {
    if (mlir::isa<StructTypeAttr>(T.getElementType()))
      return true;
    if (mlir::isa<UnionTypeAttr>(T.getElementType()))
      return true;
  }
  return false;
}

inline bool verifyFunctionReturnType(ValueType ReturnType) {
  ReturnType = dealias(ReturnType);

  if (isObjectType(ReturnType))
    return not isArrayType(ReturnType);

  return isVoid(ReturnType) or mlir::isa<ScalarTupleType>(ReturnType);
}

} // namespace mlir::clift
