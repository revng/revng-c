//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

// RUN: %revngcliftopt %s

!int32_t = !clift.primitive<SignedKind 4>
!int32_t$ptr = !clift.pointer<pointee_type = !int32_t, pointer_size = 8>
!int32_t$const = !clift.const<!int32_t>
!int32_t$const$ptr = !clift.pointer<pointee_type = !int32_t$const, pointer_size = 8>

!uint32_t = !clift.primitive<UnsignedKind 4>
!uint32_t$ptr = !clift.pointer<pointee_type = !uint32_t, pointer_size = 8>

%i = clift.undef : !int32_t
clift.cast ident %i : !int32_t -> !uint32_t

%ip = clift.undef : !int32_t$ptr
clift.cast ident %ip : !int32_t$ptr -> !uint32_t$ptr
clift.cast ident %ip : !int32_t$ptr -> !int32_t$const$ptr

%icp = clift.undef : !int32_t$const$ptr
clift.cast ident %icp : !int32_t$const$ptr -> !int32_t$ptr
