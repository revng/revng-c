;
; Copyright rev.ng Labs Srl. See LICENSE.md for details.
;

; RUN: %revngopt %s -bundle-edges-by-cycle -S -o - |& FileCheck %s

; while test

define void @f() {
entry:
  br label %while.cond

while.cond:
  br i1 undef, label %while.body, label %while.end

while.body:
  br label %while.cond

while.end:
  ret void
}

; CHECK-LABEL: void @f

; CHECK: entry:
; CHECK:  br label %entry_succ_ceci_1

; CHECK: while.cond:
; CHECK:  br i1 undef, label %while.cond_succ_ceci_0, label %while.cond_succ_ceci_1

; CHECK: while.body:
; CHECK:   br label %while.body_succ_ceci_0

; CHECK: while.end:
; CHECK:   ret void

; CHECK: while.body_pred_ceci_0:
; CHECK:   br label %while.body

; CHECK: while.body_succ_ceci_0:
; CHECK:   br label %while.cond_pred_ceci_0

; CHECK: while.end_pred_ceci_1:
; CHECK:   br label %while.end

; CHECK: while.cond_pred_ceci_0:

; CHECK: while.cond_pred_ceci_1:
; CHECK:   br label %while.cond

; CHECK: while.cond_succ_ceci_0:
; CHECK:   br label %while.body_pred_ceci_0

; CHECK: while.cond_succ_ceci_1:
; CHECK:   br label %while.end_pred_ceci_1

; CHECK: entry_succ_ceci_1:
; CHECK:   br label %while.cond_pred_ceci_1

; nested whiles test

define void @g() {
entry:
  br label %while.cond

while.cond:
  br i1 undef, label %while.body, label %while.end6

while.body:
  br label %while.cond1

while.cond1:
  br i1 undef, label %while.body3, label %while.end

while.body3:
  br label %while.cond1

while.end:
  br label %while.cond

while.end6:
  ret void
}

; CHECK-LABEL: void @g

; CHECK: entry:
; CHECK:   br label %entry_succ_ceci_2

; CHECK: while.cond:
; CHECK:   br i1 undef, label %while.cond_succ_ceci_1, label %while.cond_succ_ceci_2

; CHECK: while.body:
; CHECK:   br label %while.body_succ_ceci_1

; CHECK: while.cond1:
; CHECK:   br i1 undef, label %while.cond1_succ_ceci_0, label %while.cond1_succ_ceci_1

; CHECK: while.body3:
; CHECK:   br label %while.body3_succ_ceci_0

; CHECK: while.end:
; CHECK:   br label %while.end_succ_ceci_1

; CHECK: while.end6:
; CHECK:   ret void

; CHECK: while.body3_pred_ceci_0:
; CHECK:   br label %while.body3

; CHECK: while.body3_succ_ceci_0:
; CHECK:   br label %while.cond1_pred_ceci_0

; CHECK: while.end_pred_ceci_1:
; CHECK:   br label %while.end

; CHECK: while.end_succ_ceci_1:
; CHECK:   br label %while.cond_pred_ceci_1

; CHECK: while.cond1_pred_ceci_0:
; CHECK:   br label %while.cond1

; CHECK: while.cond1_pred_ceci_1:
; CHECK:   br label %while.cond1

; CHECK: while.cond1_succ_ceci_0:
; CHECK:   br label %while.body3_pred_ceci_0

; CHECK: while.cond1_succ_ceci_1:
; CHECK:   br label %while.end_pred_ceci_1

; CHECK: while.body_pred_ceci_1:
; CHECK:   br label %while.body

; CHECK: while.body_succ_ceci_1:
; CHECK:   br label %while.cond1_pred_ceci_1

; CHECK: while.end6_pred_ceci_2:
; CHECK:   br label %while.end6

; CHECK: while.cond_pred_ceci_1:
; CHECK:   br label %while.cond

; CHECK: while.cond_pred_ceci_2:
; CHECK:   br label %while.cond

; CHECK: while.cond_succ_ceci_1:
; CHECK:   br label %while.body_pred_ceci_1

; CHECK: while.cond_succ_ceci_2:
; CHECK:   br label %while.end6_pred_ceci_2

; CHECK: entry_succ_ceci_2:
; CHECK:   br label %while.cond_pred_ceci_2

; overlapping cycles test

define void @h() {
entry:
  br label %while.cond

while.cond:
  br i1 undef, label %while.body, label %while.end

while.body:
  br label %label

label:
  br i1 undef, label %if.then, label %if.end

if.then:
  br label %while.cond

if.end:
  br i1 undef, label %if.then4, label %if.end5

if.then4:
  br label %label

if.end5:
  br label %while.cond

while.end:
  ret void
}

; CHECK-LABEL: void @h

; CHECK: entry:
; CHECK:   br label %entry_succ_ceci_5

; CHECK: while.cond:
; CHECK:   br i1 undef, label %while.cond_succ_ceci_4, label %while.cond_succ_ceci_5

; CHECK: while.body:
; CHECK:   br label %while.body_succ_ceci_4

; CHECK: label:
; CHECK:   br i1 undef, label %label_succ_ceci_0, label %label_succ_ceci_3

; CHECK: if.then:
; CHECK:   br label %if.then_succ_ceci_0

; CHECK: if.end:
; CHECK:   br i1 undef, label %if.end_succ_ceci_1, label %if.end_succ_ceci_2

; CHECK: if.then4:
; CHECK:   br label %if.then4_succ_ceci_1

; CHECK: if.end5:
; CHECK:   br label %if.end5_succ_ceci_2

; CHECK: while.end:
; CHECK:   ret void

; CHECK: if.then_pred_ceci_0:
; CHECK:   br label %if.then

; CHECK: if.then_succ_ceci_0:
; CHECK:   br label %while.cond_pred_ceci_0

; CHECK: if.then4_pred_ceci_1:
; CHECK:   br label %if.then4

; CHECK: if.then4_succ_ceci_1:
; CHECK:   br label %label_pred_ceci_1

; CHECK: if.end5_pred_ceci_2:
; CHECK:   br label %if.end5

; CHECK: if.end5_succ_ceci_2:
; CHECK:   br label %while.cond_pred_ceci_2

; CHECK: if.end_pred_ceci_3:
; CHECK:   br label %if.end

; CHECK: if.end_succ_ceci_1:
; CHECK:   br label %if.then4_pred_ceci_1

; CHECK: if.end_succ_ceci_2:
; CHECK:   br label %if.end5_pred_ceci_2

; CHECK: label_pred_ceci_1:
; CHECK:   br label %label

; CHECK: label_pred_ceci_4:
; CHECK:   br label %label

; CHECK: label_succ_ceci_0:
; CHECK:   br label %if.then_pred_ceci_0

; CHECK: label_succ_ceci_3:
; CHECK:   br label %if.end_pred_ceci_3

; CHECK: while.body_pred_ceci_4:
; CHECK:   br label %while.body

; CHECK: while.body_succ_ceci_4:
; CHECK:   br label %label_pred_ceci_4

; CHECK: while.end_pred_ceci_5:
; CHECK:   br label %while.end

; CHECK: while.cond_pred_ceci_2:
; CHECK:   br label %while.cond

; CHECK: while.cond_pred_ceci_0:
; CHECK:   br label %while.cond

; CHECK: while.cond_pred_ceci_5:
; CHECK:   br label %while.cond

; CHECK: while.cond_succ_ceci_4:
; CHECK:   br label %while.body_pred_ceci_4

; CHECK: while.cond_succ_ceci_5:
; CHECK:   br label %while.end_pred_ceci_5

; CHECK: entry_succ_ceci_5:
; CHECK:   br label %while.cond_pred_ceci_5

; diamond test

define void @i() {
entry:
  br i1 undef, label %if.then, label %if.else

if.then:
  br label %if.end

if.else:
  br label %if.end

if.end:
  ret void
}

; CHECK-LABEL: void @i

; CHECK: entry:
; CHECK:   br i1 undef, label %entry_succ_ceci_1, label %entry_succ_ceci_0

; CHECK: if.then:
; CHECK:   br label %if.then_succ_ceci_1

; CHECK: if.else:
; CHECK:   br label %if.else_succ_ceci_0

; CHECK: if.end:
; CHECK:   ret void

; CHECK: if.end_pred_ceci_0:
; CHECK:   br label %if.end

; CHECK: if.end_pred_ceci_1:
; CHECK:   br label %if.end

; CHECK: if.then_pred_ceci_1:
; CHECK:   br label %if.then

; CHECK: if.then_succ_ceci_1:
; CHECK:   br label %if.end_pred_ceci_1

; CHECK: if.else_pred_ceci_0:
; CHECK:   br label %if.else

; CHECK: if.else_succ_ceci_0:
; CHECK:   br label %if.end_pred_ceci_0

; CHECK: entry_succ_ceci_1:
; CHECK:   br label %if.then_pred_ceci_1

; CHECK: entry_succ_ceci_0:
; CHECK:   br label %if.else_pred_ceci_0

; double edge test

define void @l() {
block_a:
  br i1 undef, label %block_b, label %block_b

block_b:
  ret void
}

; CHECK-LABEL: void @l

; CHECK: block_a:
; CHECK:   br i1 undef, label %block_a_succ_ceci_1, label %block_a_succ_ceci_0

; CHECK: block_b:
; CHECK:   ret void

; CHECK: block_b_pred_ceci_1:
; CHECK:   br label %block_b

; CHECK: block_b_pred_ceci_0:
; CHECK:   br label %block_b

; CHECK: block_a_succ_ceci_1:
; CHECK:   br label %block_b_pred_ceci_1

; CHECK: block_a_succ_ceci_0:
; CHECK:   br label %block_b_pred_ceci_0

; while self-loop test

define void @m() {
entry:
  br label %while.cond

while.cond:
  br i1 undef, label %while.body, label %while.end

while.body:
  br i1 undef, label %while.cond, label %while.body

while.end:
  ret void
}

; CHECK-LABEL: void @m

; CHECK: entry:
; CHECK:   br label %entry_succ_ceci_2

; CHECK: while.cond:
; CHECK:   br i1 undef, label %while.cond_succ_ceci_0, label %while.cond_succ_ceci_2

; CHECK: while.body:
; CHECK:   br i1 undef, label %while.body_succ_ceci_0, label %while.body_succ_ceci_1

; CHECK: while.end:
; CHECK:   ret void

; CHECK: while.body_pred_ceci_1:
; CHECK:   br label %while.body

; CHECK: while.body_pred_ceci_0:
; CHECK:   br label %while.body

; CHECK: while.body_succ_ceci_0:
; CHECK:   br label %while.cond_pred_ceci_0

; CHECK: while.body_succ_ceci_1:
; CHECK:   br label %while.body_pred_ceci_1

; CHECK: while.end_pred_ceci_2:
; CHECK:   br label %while.end

; CHECK: while.cond_pred_ceci_0:
; CHECK:   br label %while.cond

; CHECK: while.cond_pred_ceci_2:
; CHECK:   br label %while.cond

; CHECK: while.cond_succ_ceci_0:
; CHECK:   br label %while.body_pred_ceci_0

; CHECK: while.cond_succ_ceci_2:
; CHECK:   br label %while.end_pred_ceci_2

; CHECK: entry_succ_ceci_2:
; CHECK:   br label %while.cond_pred_ceci_2
