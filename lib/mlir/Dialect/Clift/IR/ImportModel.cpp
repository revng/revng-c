//
// Copyright (c) rev.ng Labs Srl. See LICENSE.md for details.
//

#include "llvm/ADT/SmallSet.h"
#include "llvm/Support/FormatVariadic.h"

#include "revng/ADT/RecursiveCoroutine.h"

#include "revng-c/mlir/Dialect/Clift/IR/Clift.h"
#include "revng-c/mlir/Dialect/Clift/IR/CliftAttributes.h"
#include "revng-c/mlir/Dialect/Clift/IR/CliftTypes.h"
#include "revng-c/mlir/Dialect/Clift/IR/ImportModel.h"

namespace {

namespace clift = mlir::clift;

template<typename Attribute>
using AttributeVector = llvm::SmallVector<Attribute, 16>;

class CliftConverter {
  mlir::MLIRContext *Context;
  llvm::function_ref<mlir::InFlightDiagnostic()> EmitError;

  llvm::DenseMap<uint64_t, clift::TypeDefinition> Cache;
  llvm::DenseMap<uint64_t, const model::Type *> IncompleteTypes;

  llvm::SmallSet<uint64_t, 16> DefinitionGuardSet;

  class DefinitionGuard {
    CliftConverter *Self = nullptr;
    uint64_t ID;

  public:
    explicit DefinitionGuard(CliftConverter &Self, const uint64_t ID) {
      if (Self.DefinitionGuardSet.insert(ID).second) {
        this->Self = &Self;
        this->ID = ID;
      }
    }

    DefinitionGuard(const DefinitionGuard &) = delete;
    DefinitionGuard &operator=(const DefinitionGuard &) = delete;

    ~DefinitionGuard() {
      if (Self != nullptr) {
        Self->DefinitionGuardSet.erase(ID);
      }
    }

    explicit operator bool() const { return Self != nullptr; }
  };

public:
  explicit CliftConverter(mlir::MLIRContext &Context,
                          llvm::function_ref<mlir::InFlightDiagnostic()>
                            EmitError = nullptr) :
    Context(&Context), EmitError(EmitError) {}

  CliftConverter(const CliftConverter &) = delete;
  CliftConverter &operator=(const CliftConverter &) = delete;

  ~CliftConverter() {
    revng_assert(DefinitionGuardSet.empty());
    revng_assert(IncompleteTypes.empty());
  }

  clift::ValueType convertType(const model::Type &ModelType) {
    const clift::ValueType T = getUnwrappedType(ModelType,
                                                /*RequireComplete=*/true);
    if (T and not processIncompleteTypes()) {
      IncompleteTypes.clear();
      return nullptr;
    }
    return T;
  }

  clift::ValueType convertQualifiedType(const model::QualifiedType &ModelType) {
    const clift::ValueType T = getQualifiedType(ModelType,
                                                /*RequireComplete=*/true);
    if (T and not processIncompleteTypes()) {
      IncompleteTypes.clear();
      return nullptr;
    }
    return T;
  }

private:
  mlir::BoolAttr getBool(bool const Value) {
    return mlir::BoolAttr::get(Context, Value);
  }

  mlir::BoolAttr getFalse() { return getBool(false); }

  template<typename T, typename... ArgTypes>
  T make(const ArgTypes &...Args) {
    if (EmitError and failed(T::verify(EmitError, Args...)))
      return {};
    return T::get(Context, Args...);
  }

  static clift::PrimitiveKind
  getPrimitiveKind(const model::PrimitiveTypeKind::Values K) {
    switch (K) {
    case model::PrimitiveTypeKind::Void:
      return clift::PrimitiveKind::VoidKind;
    case model::PrimitiveTypeKind::Generic:
      return clift::PrimitiveKind::GenericKind;
    case model::PrimitiveTypeKind::PointerOrNumber:
      return clift::PrimitiveKind::PointerOrNumberKind;
    case model::PrimitiveTypeKind::Number:
      return clift::PrimitiveKind::NumberKind;
    case model::PrimitiveTypeKind::Unsigned:
      return clift::PrimitiveKind::UnsignedKind;
    case model::PrimitiveTypeKind::Signed:
      return clift::PrimitiveKind::SignedKind;
    case model::PrimitiveTypeKind::Float:
      return clift::PrimitiveKind::FloatKind;
    default:
      revng_abort();
    }
  }

  clift::ValueType getPrimitiveType(const model::PrimitiveType &ModelType,
                                    const bool Const) {
    return make<clift::PrimitiveType>(getPrimitiveKind(ModelType
                                                         .PrimitiveKind()),
                                      ModelType.Size(),
                                      getBool(Const));
  }

  RecursiveCoroutine<clift::TypeDefinition>
  getTypeAttribute(const model::CABIFunctionType &ModelType) {
    DefinitionGuard Guard(*this, ModelType.ID());
    if (not Guard) {
      if (EmitError)
        EmitError() << "Recursive definition of CABIFunctionType "
                    << ModelType.ID();
      rc_return nullptr;
    }

    AttributeVector<clift::FunctionArgumentAttr> Args;
    Args.reserve(ModelType.Arguments().size());

    for (const model::Argument &Argument : ModelType.Arguments()) {
      const auto Type = rc_recur getQualifiedType(Argument.Type());
      if (not Type)
        rc_return nullptr;
      const llvm::StringRef Name = Argument.OriginalName();
      const auto Attribute = make<clift::FunctionArgumentAttr>(Type, Name);
      if (not Attribute)
        rc_return nullptr;
      Args.push_back(Attribute);
    }

    const auto ReturnType = rc_recur getQualifiedType(ModelType.ReturnType());
    if (not ReturnType)
      rc_return nullptr;

    rc_return make<clift::FunctionAttr>(ModelType.ID(),
                                        ModelType.OriginalName(),
                                        ReturnType,
                                        Args);
  }

  RecursiveCoroutine<clift::TypeDefinition>
  getTypeAttribute(const model::EnumType &ModelType) {
    DefinitionGuard Guard(*this, ModelType.ID());
    if (not Guard) {
      if (EmitError)
        EmitError() << "Recursive definition of EnumType " << ModelType.ID();
      rc_return nullptr;
    }

    auto const UnderlyingType = rc_recur getQualifiedType(ModelType
                                                            .UnderlyingType());
    if (not UnderlyingType)
      rc_return nullptr;

    AttributeVector<clift::EnumFieldAttr> Fields;
    Fields.reserve(ModelType.Entries().size());

    for (const model::EnumEntry &Entry : ModelType.Entries()) {
      const auto Attribute = make<clift::EnumFieldAttr>(Entry.Value(),
                                                        Entry.CustomName());
      if (not Attribute)
        rc_return nullptr;
      Fields.push_back(Attribute);
    }

    rc_return make<clift::EnumAttr>(ModelType.ID(),
                                    ModelType.OriginalName(),
                                    UnderlyingType,
                                    Fields);
  }

  RecursiveCoroutine<clift::TypeDefinition>
  getTypeAttribute(const model::RawFunctionType &ModelType) {
    DefinitionGuard Guard(*this, ModelType.ID());
    if (not Guard) {
      if (EmitError)
        EmitError() << "Recursive definition of RawFunctionType "
                    << ModelType.ID();
      rc_return nullptr;
    }

    clift::FunctionArgumentAttr StackArgument;
    size_t ArgumentsCount = 0;

    if (ModelType.StackArgumentsType().isValid()) {
      const auto Type = rc_recur
        getUnwrappedType(*ModelType.StackArgumentsType().get());
      if (not Type)
        rc_return nullptr;

      // WIP: Choose an appropriate pointer size.
      const uint64_t PointerSize = 8;
      const auto PointerType = make<clift::PointerType>(Type,
                                                        PointerSize,
                                                        getFalse());
      if (not PointerType)
        rc_return nullptr;

      StackArgument = make<clift::FunctionArgumentAttr>(PointerType, "");
      if (not StackArgument)
        rc_return nullptr;

      ++ArgumentsCount;
    }

    ArgumentsCount += ModelType.Arguments().size();
    AttributeVector<clift::FunctionArgumentAttr> Args;
    Args.reserve(ArgumentsCount);

    for (const model::NamedTypedRegister &Register : ModelType.Arguments()) {
      const auto Type = rc_recur getQualifiedType(Register.Type());
      if (not Type)
        rc_return nullptr;
      const llvm::StringRef Name = Register.OriginalName();
      const auto Argument = make<clift::FunctionArgumentAttr>(Type, Name);
      if (not Argument)
        rc_return nullptr;
      Args.push_back(Argument);
    }

    if (StackArgument)
      Args.push_back(StackArgument);

    clift::ValueType ReturnType;
    switch (ModelType.ReturnValues().size()) {
    case 0:
      ReturnType = make<clift::PrimitiveType>(clift::PrimitiveKind::VoidKind,
                                              0,
                                              getFalse());
      break;

    case 1:
      ReturnType = rc_recur
        getQualifiedType(ModelType.ReturnValues().begin()->Type());
      break;

    default:
      // WIP: Revisit multi-register return type.
      AttributeVector<clift::FieldAttr> Fields;
      Fields.reserve(ModelType.ReturnValues().size());

      uint64_t Offset = 0;
      for (const model::NamedTypedRegister &Register :
           ModelType.ReturnValues()) {
        auto const RegisterType = rc_recur getQualifiedType(Register.Type());
        if (not RegisterType)
          rc_return nullptr;
        auto const Attribute = make<clift::FieldAttr>(Offset,
                                                      RegisterType,
                                                      Register.CustomName());
        if (not Attribute)
          rc_return nullptr;
        Fields.push_back(Attribute);

        const auto Size = Register.Type().size();
        // WIP: Is it possible that the register type size cannot be computed?
        if (not Size) {
          if (EmitError)
            EmitError() << "Cannot compute register size in RawFunctionType "
                        << ModelType.ID();
          rc_return nullptr;
        }

        Offset += Register.Type().size().value();
      }

      // WIP: Revisit ID selection if the struct is kept.
      const uint64_t ID = ModelType.ID() + 1'000'000'000u;
      const std::string Name = std::string(llvm::formatv("RawFunctionType-{0}-"
                                                         "ReturnType",
                                                         ModelType.ID()));
      const auto Attribute = make<clift::StructType>(ID, Name, Offset, Fields);
      if (not Attribute)
        rc_return nullptr;
      ReturnType = make<clift::DefinedType>(clift::TypeDefinition(Attribute),
                                            getFalse());
      break;
    }
    if (not ReturnType)
      rc_return nullptr;

    rc_return make<clift::FunctionAttr>(ModelType.ID(),
                                        ModelType.OriginalName(),
                                        ReturnType,
                                        Args);
  }

  RecursiveCoroutine<clift::TypeDefinition>
  getTypeAttribute(const model::StructType &ModelType,
                   const bool RequireComplete) {
    if (not RequireComplete) {
      const auto T = clift::StructType::get(Context,
                                            ModelType.ID(),
                                            ModelType.OriginalName(),
                                            ModelType.Size());
      if (not T.isDefinition())
        IncompleteTypes.try_emplace(ModelType.ID(), &ModelType);
      rc_return T;
    }

    DefinitionGuard Guard(*this, ModelType.ID());
    if (not Guard) {
      if (EmitError)
        EmitError() << "Recursive definition of StructType " << ModelType.ID();
      rc_return nullptr;
    }

    AttributeVector<clift::FieldAttr> Fields;
    Fields.reserve(ModelType.Fields().size());

    for (const model::StructField &Field : ModelType.Fields()) {
      const auto FieldType = rc_recur
        getQualifiedType(Field.Type(), /*RequireComplete=*/true);
      if (not FieldType)
        rc_return nullptr;
      const auto Attribute = make<clift::FieldAttr>(Field.Offset(),
                                                    FieldType,
                                                    Field.CustomName());
      if (not Attribute)
        rc_return nullptr;
      Fields.push_back(Attribute);
    }

    rc_return make<clift::StructType>(ModelType.ID(),
                                      ModelType.OriginalName(),
                                      ModelType.Size(),
                                      Fields);
  }

  RecursiveCoroutine<clift::TypeDefinition>
  getTypeAttribute(const model::TypedefType &ModelType,
                   const bool RequireComplete) {
    std::optional<DefinitionGuard> Guard;

    if (RequireComplete) {
      Guard.emplace(*this, ModelType.ID());
      if (not *Guard) {
        if (EmitError)
          EmitError() << "Recursive definition of TypedefType "
                      << ModelType.ID();
        rc_return nullptr;
      }
    }

    auto const UnderlyingType = rc_recur
      getQualifiedType(ModelType.UnderlyingType(), RequireComplete);
    if (not UnderlyingType)
      rc_return nullptr;
    rc_return make<clift::TypedefAttr>(ModelType.ID(),
                                       ModelType.OriginalName(),
                                       UnderlyingType);
  }

  RecursiveCoroutine<clift::TypeDefinition>
  getTypeAttribute(const model::UnionType &ModelType,
                   const bool RequireComplete) {
    if (not RequireComplete) {
      const auto T = clift::UnionType::get(Context,
                                           ModelType.ID(),
                                           ModelType.OriginalName());
      if (not T.isDefinition())
        IncompleteTypes.try_emplace(ModelType.ID(), &ModelType);
      rc_return T;
    }

    DefinitionGuard Guard(*this, ModelType.ID());
    if (not Guard) {
      if (EmitError)
        EmitError() << "Recursive definition of UnionType " << ModelType.ID();
      rc_return nullptr;
    }

    AttributeVector<clift::FieldAttr> Fields;
    Fields.reserve(ModelType.Fields().size());

    for (const model::UnionField &Field : ModelType.Fields()) {
      auto const FieldType = rc_recur
        getQualifiedType(Field.Type(), /*RequireComplete=*/true);
      if (not FieldType)
        rc_return nullptr;
      auto const Attribute = make<clift::FieldAttr>(0,
                                                    FieldType,
                                                    Field.CustomName());
      if (not Attribute)
        rc_return nullptr;
      Fields.push_back(Attribute);
    }

    rc_return make<clift::UnionType>(ModelType.ID(),
                                     ModelType.OriginalName(),
                                     Fields);
  }

  RecursiveCoroutine<clift::TypeDefinition>
  getTypeAttribute(const model::Type &ModelType, bool &RequireComplete) {
    switch (ModelType.Kind()) {
    case model::TypeKind::CABIFunctionType: {
      RequireComplete = true;
      auto const &T = llvm::cast<model::CABIFunctionType>(ModelType);
      return getTypeAttribute(T);
    }
    case model::TypeKind::EnumType: {
      RequireComplete = true;
      auto const &T = llvm::cast<model::EnumType>(ModelType);
      return getTypeAttribute(T);
    }
    case model::TypeKind::RawFunctionType: {
      RequireComplete = true;
      auto const &T = llvm::cast<model::RawFunctionType>(ModelType);
      return getTypeAttribute(T);
    }
    case model::TypeKind::StructType: {
      auto const &T = llvm::cast<model::StructType>(ModelType);
      return getTypeAttribute(T, RequireComplete);
    }
    case model::TypeKind::TypedefType: {
      auto const &T = llvm::cast<model::TypedefType>(ModelType);
      return getTypeAttribute(T, RequireComplete);
    }
    case model::TypeKind::UnionType: {
      auto const &T = llvm::cast<model::UnionType>(ModelType);
      return getTypeAttribute(T, RequireComplete);
    }
    default:
      revng_abort();
    }
  }

  RecursiveCoroutine<clift::ValueType>
  getUnwrappedType(const model::Type &ModelType,
                   bool RequireComplete = false,
                   const bool Const = false) {
    if (const auto *const T = llvm::dyn_cast<model::PrimitiveType>(&ModelType))
      rc_return getPrimitiveType(*T, Const);

    auto const getDefinedType = [&](const auto Attr) -> clift::ValueType {
      return make<clift::DefinedType>(Attr, getBool(Const));
    };

    if (const auto It = Cache.find(ModelType.ID()); It != Cache.end())
      rc_return getDefinedType(It->second);

    const clift::TypeDefinition Attr = getTypeAttribute(ModelType,
                                                        RequireComplete);

    if (not Attr)
      rc_return nullptr;

    if (RequireComplete) {
      const auto R = Cache.try_emplace(ModelType.ID(), Attr);
      revng_assert(R.second);
    }

    rc_return getDefinedType(Attr);
  }

  RecursiveCoroutine<clift::ValueType>
  getQualifiedType(const model::QualifiedType &ModelType,
                   bool RequireComplete = false) {
    if (not ModelType.UnqualifiedType().isValid()) {
      if (EmitError)
        EmitError() << "Invalid UnqualifiedType in QualifiedType";
      rc_return nullptr;
    }

    auto Qualifiers = llvm::ArrayRef(ModelType.Qualifiers());

    const auto takeQualifier = [&]() -> const model::Qualifier & {
      const model::Qualifier &Qualifier = Qualifiers.back();
      Qualifiers = Qualifiers.slice(0, Qualifiers.size() - 1);
      return Qualifier;
    };

    const auto takeConst = [&]() -> bool {
      if (not Qualifiers.empty()
          and Qualifiers.back().Kind() == model::QualifierKind::Const) {
        Qualifiers = Qualifiers.slice(0, Qualifiers.size() - 1);
        return true;
      }
      return false;
    };

    // If the set of qualifiers contains any pointers,
    // the base type does not need to be complete.
    for (const model::Qualifier &Qualifier : Qualifiers) {
      if (Qualifier.Kind() == model::QualifierKind::Pointer) {
        RequireComplete = false;
        break;
      }
    }

    clift::ValueType QualifiedType = rc_recur
      getUnwrappedType(*ModelType.UnqualifiedType().get(),
                       RequireComplete,
                       takeConst());

    if (not QualifiedType)
      rc_return nullptr;

    // Loop over (qualifier, const (optional)) pairs wrapping the type at each
    // iteration, until the list of qualifiers is exhausted.
    while (not Qualifiers.empty()) {
      switch (const model::Qualifier &Qualifier = takeQualifier();
              Qualifier.Kind()) {
      case model::QualifierKind::Pointer:
        QualifiedType = make<clift::PointerType>(QualifiedType,
                                                 Qualifier.Size(),
                                                 getBool(takeConst()));
        break;

      case model::QualifierKind::Array:
        QualifiedType = make<clift::ArrayType>(QualifiedType,
                                               Qualifier.Size(),
                                               getBool(takeConst()));
        break;

      default:
        if (EmitError)
          EmitError() << "invalid type qualifiers";
        rc_return nullptr;
      }

      if (not QualifiedType)
        rc_return nullptr;
    }

    rc_return QualifiedType;
  }

  bool processIncompleteTypes() {
    while (not IncompleteTypes.empty()) {
      const auto Iterator = IncompleteTypes.begin();
      const model::Type &ModelType = *Iterator->second;
      IncompleteTypes.erase(Iterator);

      const clift::ValueType T = getUnwrappedType(ModelType,
                                                  /*RequireComplete=*/true);

      if (not T)
        return false;
    }
    return true;
  }
};

} // namespace

clift::ValueType revng::getUnqualifiedType(mlir::MLIRContext &Context,
                                           const model::Type &ModelType) {
  return CliftConverter(Context).convertType(ModelType);
}

clift::ValueType
revng::getUnqualifiedTypeChecked(llvm::function_ref<mlir::InFlightDiagnostic()>
                                   EmitError,
                                 mlir::MLIRContext &Context,
                                 const model::Type &ModelType) {
  return CliftConverter(Context, EmitError).convertType(ModelType);
}

clift::ValueType
revng::getQualifiedType(mlir::MLIRContext &Context,
                        const model::QualifiedType &ModelType) {
  return CliftConverter(Context).convertQualifiedType(ModelType);
}

clift::ValueType
revng::getQualifiedTypeChecked(llvm::function_ref<mlir::InFlightDiagnostic()>
                                 EmitError,
                               mlir::MLIRContext &Context,
                               const model::QualifiedType &ModelType) {
  return CliftConverter(Context, EmitError).convertQualifiedType(ModelType);
}
