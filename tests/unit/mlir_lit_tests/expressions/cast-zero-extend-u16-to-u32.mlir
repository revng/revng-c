//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

// RUN: %revngcliftopt %s

!uint16_t = !clift.primitive<UnsignedKind 2>
!uint32_t = !clift.primitive<UnsignedKind 4>

%value = clift.undef : !uint16_t
clift.cast<zext> %value : !uint16_t -> !uint32_t
