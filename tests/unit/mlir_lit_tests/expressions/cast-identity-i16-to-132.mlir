//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

// RUN: not %revngcliftopt %s 2>&1 | FileCheck %s

!int16_t = !clift.primitive<SignedKind 2>
!int32_t = !clift.primitive<SignedKind 4>

%i = clift.undef : !int16_t

// CHECK: result and argument types must be equal in size
clift.cast<ident> %i : !int16_t -> !int32_t
