//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

// RUN: not %revngcliftopt %s 2>&1 | FileCheck %s

!int32_t = !clift.primitive<SignedKind 4>

!s = !clift.defined<#clift.struct<
  id = 1,
  name = "",
  size = 1,
  fields = [<
      offset = 0,
      name = "x",
      type = !int32_t
    >
  ]
>>

%0 = clift.undef : !s

// CHECK: class member index out of range
%1 = clift.object_access %0 1 : !s -> !int32_t
