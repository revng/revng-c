;
; This file is distributed under the MIT License. See LICENSE.md for details.
;

; RUN: %revngopt %s -scope-graph-logger -debug-log=scope-graph-logger -o /dev/null |& FileCheck %s

; no dashed edge test

define void @f() {
block_a:
  br i1 undef, label %block_b, label %block_c

block_b:
  ret void

block_c:
  br i1 undef, label %block_b, label %block_e

block_e:
  ret void
}

; CHECK-LABEL: ScopeGraph of function: f
; CHECK-NEXT: Block block_a successors:
; CHECK-NEXT:   block_b
; CHECK-NEXT:   block_c
; CHECK-NEXT: Block block_b successors:
; CHECK-NEXT: Block block_c successors:
; CHECK-NEXT:   block_b
; CHECK-NEXT:   block_e
; CHECK-NEXT: Block block_e successors:

; scope edge test

define void @g() {
block_a:
  br i1 undef, label %block_b, label %block_c

block_b:
  call void @scope_closer(ptr blockaddress(@g, %block_b))
  ret void

block_c:
  br i1 undef, label %block_b, label %block_e

block_e:
  ret void
}

declare void @scope_closer(ptr)

; CHECK-LABEL: ScopeGraph of function: g
; CHECK-NEXT: Block block_a successors:
; CHECK-NEXT:   block_b
; CHECK-NEXT:   block_c
; CHECK-NEXT: Block block_b successors:
; CHECK-NEXT:   block_b
; CHECK-NEXT: Block block_c successors:
; CHECK-NEXT:   block_b
; CHECK-NEXT:   block_e
; CHECK-NEXT: Block block_e successors:
