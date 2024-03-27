//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include <set>

#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/TypeSwitch.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/DialectImplementation.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/OpImplementation.h"

#include "revng-c/mlir/Dialect/Clift/IR/Clift.h"
#include "revng-c/mlir/Dialect/Clift/IR/CliftAttributes.h"
#include "revng-c/mlir/Dialect/Clift/IR/CliftInterfaces.h"
#include "revng-c/mlir/Dialect/Clift/IR/CliftTypes.h"

// This include should stay here for correct build procedure
//
#define GET_ATTRDEF_CLASSES
#include "revng-c/mlir/Dialect/Clift/IR/CliftAttributes.cpp.inc"

using EmitErrorType = llvm::function_ref<mlir::InFlightDiagnostic()>;

static thread_local std::map<uint64_t, mlir::Attribute> CurrentlyVisitedTypes;

static const mlir::Attribute *getVisitedType(const uint64_t ID) {
  auto const Iterator = CurrentlyVisitedTypes.find(ID);
  return Iterator == CurrentlyVisitedTypes.end() ? nullptr : &Iterator->second;
}

[[nodiscard]] static auto onTypeVisited(const uint64_t ID,
                                        const mlir::Attribute &Attr) {
  const auto R = CurrentlyVisitedTypes.try_emplace(ID, Attr);
  revng_assert(R.second);

  return llvm::make_scope_exit([=]() {
    const auto N = CurrentlyVisitedTypes.erase(ID);
    revng_assert(N == 1);
  });
}

void mlir::clift::CliftDialect::registerAttributes() {
  addAttributes<StructType, UnionType, /* Include the auto-generated clift types
                                        */
#define GET_ATTRDEF_LIST
#include "revng-c/mlir/Dialect/Clift/IR/CliftAttributes.cpp.inc"
                /* End of types list */>();
}

mlir::LogicalResult mlir::clift::FieldAttr::verify(EmitErrorType EmitError,
                                                   uint64_t Offset,
                                                   mlir::Type ElementType,
                                                   llvm::StringRef Name) {
  if (auto Definition = mlir::dyn_cast<mlir::clift::DefinedType>(ElementType))
    if (mlir::isa<mlir::clift::FunctionAttr>(Definition.getElementType()))
      return EmitError() << "Underlying type of field attr cannot be a "
                            "function type";
  mlir::clift::ValueType
    Casted = mlir::dyn_cast<mlir::clift::ValueType>(ElementType);
  if (Casted == nullptr) {
    return EmitError() << "Underlying type of a field attr must be a value "
                          "type";
  }
  if (Casted.getByteSize() == 0) {
    return EmitError() << "Field cannot be of zero size";
  }
  return mlir::success();
}

using EnumFieldAttr = mlir::clift::EnumFieldAttr;
mlir::LogicalResult EnumFieldAttr::verify(EmitErrorType EmitError,
                                          uint64_t RawValue,
                                          llvm::StringRef Name) {
  return mlir::success();
}

using TypedefAttr = mlir::clift::TypedefAttr;
mlir::LogicalResult TypedefAttr::verify(EmitErrorType EmitError,
                                        uint64_t Id,
                                        llvm::StringRef Name,
                                        mlir::clift::ValueType UnderlyingType) {
  return mlir::success();
}

using ArgAttr = mlir::clift::FunctionArgumentAttr;
mlir::LogicalResult ArgAttr::verify(EmitErrorType EmitError,
                                    mlir::clift::ValueType underlying,
                                    llvm::StringRef Name) {
  if (underlying.getByteSize() == 0) {
    return EmitError() << "type of argument of function cannot be zero size";
  }
  return mlir::success();
}

using mlir::clift::FunctionAttr;
mlir::LogicalResult
FunctionAttr::verify(EmitErrorType EmitError,
                     uint64_t Id,
                     llvm::StringRef,
                     mlir::clift::ValueType ReturnType,
                     llvm::ArrayRef<mlir::clift::FunctionArgumentAttr> Args) {
  if (const auto Type = mlir::dyn_cast<mlir::clift::DefinedType>(ReturnType)) {
    if (mlir::isa<FunctionAttr>(Type.getElementType()))
      return EmitError() << "function type cannot return another function type";
  }

  return mlir::success();
}

/// Parse a type registered to this dialect
::mlir::Attribute
mlir::clift::CliftDialect::parseAttribute(::mlir::DialectAsmParser &Parser,
                                          mlir::Type Type) const {
  ::llvm::SMLoc typeLoc = Parser.getCurrentLocation();
  ::llvm::StringRef Mnemonic;
  ::mlir::Attribute GenAttr;

  auto ParseResult = generatedAttributeParser(Parser, &Mnemonic, Type, GenAttr);
  if (ParseResult.has_value())
    return GenAttr;
  if (Mnemonic == StructType::getMnemonic()) {
    return StructType::parse(Parser);
  }
  if (Mnemonic == UnionType::getMnemonic()) {
    return UnionType::parse(Parser);
  }

  Parser.emitError(typeLoc) << "unknown  attr `" << Mnemonic << "` in dialect `"
                            << getNamespace() << "`";
  return {};
}

/// Print a type registered to this dialect
void mlir::clift::CliftDialect::printAttribute(::mlir::Attribute Attr,
                                               ::mlir::DialectAsmPrinter
                                                 &Printer) const {

  if (::mlir::succeeded(generatedAttributePrinter(Attr, Printer)))
    return;
  if (auto Casted = Attr.dyn_cast<StructType>()) {
    Casted.print(Printer);
    return;
  }
  if (auto Casted = Attr.dyn_cast<UnionType>()) {
    Casted.print(Printer);
    return;
  }
  revng_abort("cannot print attribute");
}

template<typename AttrType>
static mlir::Attribute printImpl(mlir::AsmPrinter &P, AttrType Attr) {
  revng_assert(Attr.getImpl()->isInitialized());

  const uint64_t ID = Attr.getImpl()->getID();

  P << Attr.getMnemonic();
  P << "<id = ";
  P << ID;

  if (getVisitedType(ID) != nullptr) {
    P << ">";
    return Attr;
  }

  P << ", name = ";
  P << "\"" << Attr.getName() << "\"";

  if constexpr (std::is_same_v<AttrType, mlir::clift::StructType>) {
    P << ", ";
    P.printKeywordOrString("size");
    P << " = ";
    P << Attr.getByteSize();
  }

  auto Guard = onTypeVisited(ID, Attr);

  P << ", fields = [";
  P.printStrippedAttrOrType(Attr.getImpl()->getFields());
  P << "]>";

  return Attr;
}

mlir::Attribute mlir::clift::UnionType::print(AsmPrinter &p) const {
  return printImpl(p, *this);
}

mlir::Attribute mlir::clift::StructType::print(AsmPrinter &p) const {
  return printImpl(p, *this);
}

template<typename AttrType>
static AttrType parseImpl(mlir::AsmParser &parser, llvm::StringRef TypeName) {
  const auto OnUnexpectedToken = [&parser,
                                  TypeName](llvm::StringRef name) -> AttrType {
    parser.emitError(parser.getCurrentLocation(),
                     "Expected " + name + " while parsing mlir " + TypeName
                       + "type");
    return AttrType();
  };

  if (parser.parseLess()) {
    return OnUnexpectedToken("<");
  }

  if (parser.parseKeyword("id").failed()) {
    return OnUnexpectedToken("keyword 'id'");
  }

  if (parser.parseEqual().failed()) {
    return OnUnexpectedToken("=");
  }

  uint64_t ID;
  if (parser.parseInteger(ID).failed()) {
    return OnUnexpectedToken("<integer>");
  }

  if (const auto Attr = getVisitedType(ID)) {
    if (parser.parseGreater().failed()) {
      return OnUnexpectedToken(">");
    }

    return Attr->cast<AttrType>();
  }

  if (parser.parseComma().failed()) {
    return OnUnexpectedToken(",");
  }

  if (parser.parseKeyword("name").failed()) {
    return OnUnexpectedToken("keyword 'name'");
  }

  if (parser.parseEqual().failed()) {
    return OnUnexpectedToken("=");
  }

  std::string OptionalName = "";
  if (parser.parseOptionalString(&OptionalName).failed()) {
    return OnUnexpectedToken("<string>");
  }

  uint64_t Size;
  if constexpr (std::is_same_v<mlir::clift::StructType, AttrType>) {
    if (parser.parseComma().failed()) {
      return OnUnexpectedToken(",");
    }

    if (parser.parseKeyword("size").failed()) {
      return OnUnexpectedToken("keyword 'size'");
    }

    if (parser.parseEqual().failed()) {
      return OnUnexpectedToken("=");
    }

    if (parser.parseInteger(Size).failed()) {
      return OnUnexpectedToken("<uint64_t>");
    }
  }

  AttrType ToReturn;
  if constexpr (std::is_same_v<mlir::clift::StructType, AttrType>) {
    ToReturn = AttrType::get(parser.getContext(), ID, OptionalName, Size);
  } else {
    ToReturn = AttrType::get(parser.getContext(), ID, OptionalName);
  }
  auto Guard = onTypeVisited(ID, ToReturn);

  if (parser.parseComma().failed()) {
    return OnUnexpectedToken(",");
  }

  if (parser.parseKeyword("fields").failed()) {
    return OnUnexpectedToken("keyword 'fields'");
  }

  if (parser.parseEqual().failed()) {
    return OnUnexpectedToken("=");
  }

  if (parser.parseLSquare().failed()) {
    return OnUnexpectedToken("[");
  }

  using FieldsVectorType = ::llvm::SmallVector<mlir::clift::FieldAttr>;
  using FieldsParserType = ::mlir::FieldParser<FieldsVectorType>;
  ::mlir::FailureOr<FieldsVectorType> Fields(FieldsVectorType{});

  if (parser.parseOptionalRSquare().failed()) {
    Fields = FieldsParserType::parse(parser);

    if (::mlir::failed(Fields)) {
      parser.emitError(parser.getCurrentLocation(),
                       "failed to parse Clift_EnumAttr parameter 'fields' "
                       "which is to be a "
                       "`::llvm::ArrayRef<mlir::clift::FieldAttr>`");
    }

    if (parser.parseRSquare().failed()) {
      return OnUnexpectedToken("]");
    }
  }

  if (parser.parseGreater().failed()) {
    return OnUnexpectedToken(">");
  }

  ToReturn.setBody(*Fields);
  return ToReturn;
}

mlir::Attribute mlir::clift::UnionType::parse(AsmParser &parser) {
  return parseImpl<UnionType>(parser, "union");
}

mlir::Attribute mlir::clift::StructType::parse(AsmParser &parser) {
  return parseImpl<StructType>(parser, "struct");
}

static bool isIncompleteType(const mlir::Type T) {
  if (T.isa<mlir::clift::DefinedType>()) {
    const mlir::clift::TypeDefinition D = T.cast<mlir::clift::DefinedType>()
                                            .getElementType();
    if (D.isa<mlir::clift::StructType>())
      return not D.cast<mlir::clift::StructType>().isDefinition();
    if (D.isa<mlir::clift::UnionType>())
      return not D.cast<mlir::clift::UnionType>().isDefinition();
  }
  return false;
}

mlir::LogicalResult mlir::clift::StructType::verify(EmitErrorType EmitError,
                                                    uint64_t ID,
                                                    llvm::StringRef Name,
                                                    uint64_t Size) {
  if (Size == 0)
    return EmitError() << "struct type cannot have a size of zero";

  return mlir::success();
}

mlir::LogicalResult
mlir::clift::StructType::verify(const EmitErrorType EmitError,
                                const uint64_t ID,
                                const llvm::StringRef Name,
                                const uint64_t Size,
                                const llvm::ArrayRef<FieldAttr> Fields) {
  if (Size == 0)
    return EmitError() << "struct type cannot have a size of zero";

  if (not Fields.empty()) {
    uint64_t LastEndOffset = 0;

    for (const auto &Field : Fields) {
      if (isIncompleteType(Field.getType()))
        return EmitError() << "Fields of structs must be complete types";

      if (Field.getOffset() < LastEndOffset)
        return EmitError() << "Fields of structs must be ordered by offset, "
                              "and "
                              "they cannot overlap";

      LastEndOffset = Field.getOffset()
                      + Field.getType()
                          .cast<mlir::clift::ValueType>()
                          .getByteSize();
    }

    if (LastEndOffset > Size)
      return EmitError() << "offset + size of field of struct type is greater "
                            "than the struct type size.";
  }

  return mlir::success();
}

mlir::LogicalResult mlir::clift::UnionType::verify(EmitErrorType EmitError,
                                                   uint64_t ID,
                                                   llvm::StringRef Name) {
  return mlir::success();
}

mlir::LogicalResult
mlir::clift::UnionType::verify(EmitErrorType EmitError,
                               uint64_t ID,
                               llvm::StringRef,
                               llvm::ArrayRef<FieldAttr> Fields) {
  if (Fields.empty())
    return EmitError() << "union types must have at least one field";

  for (const auto &Field : Fields) {
    if (isIncompleteType(Field.getType()))
      return EmitError() << "Fields of unions must be complete types";

    if (Field.getOffset() != 0)
      return EmitError() << "union types offsets must be zero";
  }

  return mlir::success();
}

mlir::clift::StructType mlir::clift::StructType::get(MLIRContext *Context,
                                                     uint64_t ID,
                                                     llvm::StringRef Name,
                                                     uint64_t Size) {
  return Base::get(Context, ID, Name, Size);
}

mlir::clift::StructType
mlir::clift::StructType::getChecked(EmitErrorType EmitError,
                                    MLIRContext *Context,
                                    uint64_t ID,
                                    llvm::StringRef Name,
                                    uint64_t Size) {
  if (failed(verify(EmitError, ID, Name, Size)))
    return {};
  return get(Context, ID, Name, Size);
}

mlir::clift::StructType
mlir::clift::StructType::get(MLIRContext *Context,
                             uint64_t ID,
                             llvm::StringRef Name,
                             uint64_t Size,
                             llvm::ArrayRef<FieldAttr> Fields) {
  auto Result = Base::get(Context, ID, Name, Size);
  Result.setBody(Fields);
  return Result;
}

mlir::clift::StructType
mlir::clift::StructType::getChecked(EmitErrorType EmitError,
                                    MLIRContext *Context,
                                    uint64_t ID,
                                    llvm::StringRef Name,
                                    uint64_t Size,
                                    llvm::ArrayRef<FieldAttr> Fields) {
  if (failed(verify(EmitError, ID, Name, Size, Fields)))
    return {};
  return get(Context, ID, Name, Size, Fields);
}

mlir::clift::UnionType mlir::clift::UnionType::get(MLIRContext *Context,
                                                   uint64_t ID,
                                                   llvm::StringRef Name) {
  // Call into the base to get a uniqued instance of this type. The parameter
  // (name) is passed after the context.
  return Base::get(Context, ID, Name);
}

mlir::clift::UnionType
mlir::clift::UnionType::getChecked(EmitErrorType EmitError,
                                   MLIRContext *Context,
                                   uint64_t ID,
                                   llvm::StringRef Name) {
  if (failed(verify(EmitError, ID, Name)))
    return {};
  return get(Context, ID, Name);
}

mlir::clift::UnionType
mlir::clift::UnionType::get(MLIRContext *Context,
                            uint64_t ID,
                            llvm::StringRef Name,
                            llvm::ArrayRef<FieldAttr> Fields) {
  // Call into the base to get a uniqued instance of this type. The parameter
  // (name) is passed after the context.
  auto Result = Base::get(Context, ID, Name);
  Result.setBody(Fields);
  return Result;
}

mlir::clift::UnionType
mlir::clift::UnionType::getChecked(EmitErrorType EmitError,
                                   MLIRContext *Context,
                                   uint64_t ID,
                                   llvm::StringRef Name,
                                   llvm::ArrayRef<FieldAttr> Fields) {
  if (failed(verify(EmitError, ID, Name, Fields)))
    return {};
  return get(Context, ID, Name, Fields);
}
