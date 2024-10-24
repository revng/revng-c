//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

// RUN: not %revngcliftopt %s 2>&1 | FileCheck %s

!uint32_t = !clift.primitive<UnsignedKind 4>

%value = clift.undef : !uint32_t

// CHECK: result type must be wider than the argument type
clift.cast zext %value : !uint32_t -> !uint32_t
