//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

// RUN: not %revngcliftopt %s 2>&1 | FileCheck %s

!b = !clift.primitive<UnsignedKind 1>

!s = !clift.defined<#clift.struct<id = 1,
                                  name = "",
                                  size = 1,
                                  fields = []>>

!u = !clift.defined<#clift.union<id = 1,
                                 name = "",
                                 fields = [<offset = 0, name = "", type = !b>]>>

// CHECK: two distinct type definitions with the same ID
clift.module {
} {
  s = !s,
  u = !u
}
