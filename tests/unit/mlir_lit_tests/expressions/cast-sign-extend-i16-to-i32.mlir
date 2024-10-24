//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

// RUN: %revngcliftopt %s

!int16_t = !clift.primitive<SignedKind 2>
!int32_t = !clift.primitive<SignedKind 4>

%value = clift.undef : !int16_t
clift.cast sext %value : !int16_t -> !int32_t
