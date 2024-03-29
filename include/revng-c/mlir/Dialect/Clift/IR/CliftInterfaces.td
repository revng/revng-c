//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#ifndef MLIR_CLIFT_INTERFACE
#define MLIR_CLIFT_INTERFACE

include "mlir/IR/OpBase.td"

class Clift_TypeInterface<string name> : TypeInterface<name> {
    let cppNamespace = "::mlir::clift";
}

class Clift_AttrInterface<string name> : AttrInterface<name> {
    let cppNamespace = "::mlir::clift";
}

def AliasableAttr: Clift_AttrInterface<"AliasableAttr"> {
    let description = [{
      A Aliasable type is a type with a size and a constness
    }];

    let methods = [
        InterfaceMethod<
            "returns the alias of the type.",
            "std::string", "getAlias", (ins),
            /*methodBody=*/[{}], /*defaultImplementation=*/[{}]
        >,
    ];
}

def AliasableType: Clift_TypeInterface<"AliasableType"> {
    let description = [{
      A Aliasable type is a type with a size and a constness
    }];

    let methods = [
        InterfaceMethod<
            "returns the alias of the type.",
            "std::string", "getAlias", (ins),
            /*methodBody=*/[{}], /*defaultImplementation=*/[{}]
        >,
    ];
}

def SizedType : Clift_AttrInterface<"SizedType"> {
    let description = [{
      A sized type is a type with a size
    }];

    let methods = [
        InterfaceMethod<
            "returns the size of the type.",
            "uint64_t", "getByteSize", (ins),
            /*methodBody=*/[{}], /*defaultImplementation=*/[{
              return $_attr.getSize();
            }]
        >,
    ];
}

def ValueType : Clift_TypeInterface<"ValueType"> {
    let description = [{
      A value type is a type with a size and a constness
    }];

    let methods = [
        InterfaceMethod<
            "returns the size of the type.",
            "uint64_t", "getByteSize", (ins),
            /*methodBody=*/[{}], /*defaultImplementation=*/[{
              return $_type.getSize();
            }]
        >,
        InterfaceMethod<
            "the constness of the type",
            "bool", "isConst", (ins),
             /*methodBody=*/[{}], /*defaultImplemetation=*/[{
              return $_type.getIsConst().getValue();
        }]>,
    ];
}

def TypeDefinition : Clift_AttrInterface<"TypeDefinition"> {
    let description = [{
      A value type is a type with a ID and Name
    }];

    let methods = [
        InterfaceMethod<
            "the the unique id of the type",
            "uint64_t", "id", (ins),
            /*methodBody=*/[{}], /*defaultImplementation=*/[{
              return $_attr.getId();
            }]
        >,
        InterfaceMethod<
            "returns the name of the type.",
            "llvm::StringRef", "name", (ins),
             /*methodBody=*/[{}], /*defaultImplemetation=*/[{
              return $_attr.getName();
        }]>,
    ];

}

def AnyValueType: Type<CPred<"::llvm::isa<::mlir::clift::ValueType>($_self)">, "cliftValue", "::mlir::clift::ValueType">;
#endif

