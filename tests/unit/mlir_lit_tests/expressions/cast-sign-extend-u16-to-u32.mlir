//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

// RUN: not %revngcliftopt %s 2>&1 | FileCheck %s

!uint16_t = !clift.primitive<UnsignedKind 2>
!uint32_t = !clift.primitive<UnsignedKind 4>

%value = clift.undef : !uint16_t

// CHECK: result must have signed integer type
clift.cast<sext> %value : !uint16_t -> !uint32_t
