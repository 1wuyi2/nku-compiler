; SysY Part 2 - LLVM IR implementation
; Compatible with LLVM/Clang typed pointers

source_filename = "main.sy"

declare i32 @getint()
declare void @putint(i32)
declare void @putch(i32)

; ------------------------------------------------------------
; int calc(int n)
; ------------------------------------------------------------
define i32 @calc(i32 %n_arg) {
entry:
  %values = alloca [8 x i32], align 16
  %i = alloca i32, align 4
  %result = alloca i32, align 4
  %n = alloca i32, align 4

  store i32 %n_arg, i32* %n, align 4
  store i32 0, i32* %i, align 4
  store i32 0, i32* %result, align 4

  br label %loop.cond

loop.cond:
  %i_val = load i32, i32* %i, align 4
  %n_val = load i32, i32* %n, align 4
  %loop_cmp = icmp slt i32 %i_val, %n_val
  br i1 %loop_cmp, label %loop.body, label %loop.end

loop.body:
  %i_cur = load i32, i32* %i, align 4
  %i_plus_1 = add nsw i32 %i_cur, 1

  %mod2 = srem i32 %i_plus_1, 2
  %is_even = icmp eq i32 %mod2, 0

  %idx64 = sext i32 %i_cur to i64

  %elem_ptr = getelementptr inbounds [8 x i32],
              [8 x i32]* %values,
              i64 0,
              i64 %idx64

  br i1 %is_even, label %if.then, label %if.else

if.then:
  %square = mul nsw i32 %i_plus_1, %i_plus_1
  store i32 %square, i32* %elem_ptr, align 4
  br label %if.end

if.else:
  %negative = sub nsw i32 0, %i_plus_1
  store i32 %negative, i32* %elem_ptr, align 4
  br label %if.end

if.end:
  %elem_val = load i32, i32* %elem_ptr, align 4

  %result_old = load i32, i32* %result, align 4
  %result_new = add nsw i32 %result_old, %elem_val
  store i32 %result_new, i32* %result, align 4

  store i32 %i_plus_1, i32* %i, align 4

  br label %loop.cond

loop.end:
  %final_result = load i32, i32* %result, align 4
  ret i32 %final_result
}

; ------------------------------------------------------------
; int main()
; ------------------------------------------------------------
define i32 @main() {
entry:
  %n = alloca i32, align 4
  %result = alloca i32, align 4
  %quotient = alloca i32, align 4
  %remainder = alloca i32, align 4

  %input = call i32 @getint()
  store i32 %input, i32* %n, align 4

  ; if (n < 1)
  %n0 = load i32, i32* %n, align 4
  %below_lower = icmp slt i32 %n0, 1
  br i1 %below_lower, label %set.lower, label %check.upper

set.lower:
  store i32 1, i32* %n, align 4
  br label %bounds.end

check.upper:
  ; else if (n > 8)
  %n1 = load i32, i32* %n, align 4
  %above_upper = icmp sgt i32 %n1, 8
  br i1 %above_upper, label %set.upper, label %bounds.end

set.upper:
  store i32 8, i32* %n, align 4
  br label %bounds.end

bounds.end:
  %n2 = load i32, i32* %n, align 4

  ; result = calc(n)
  %calc_result = call i32 @calc(i32 %n2)
  store i32 %calc_result, i32* %result, align 4

  ; quotient = n / 2
  %q = sdiv i32 %n2, 2
  store i32 %q, i32* %quotient, align 4

  ; remainder = n % 2
  %r = srem i32 %n2, 2
  store i32 %r, i32* %remainder, align 4

  ; result = result + quotient + remainder + BASE
  %r0 = load i32, i32* %result, align 4
  %q0 = load i32, i32* %quotient, align 4
  %sum1 = add nsw i32 %r0, %q0

  %rem0 = load i32, i32* %remainder, align 4
  %sum2 = add nsw i32 %sum1, %rem0

  ; BASE = 3
  %sum3 = add nsw i32 %sum2, 3
  store i32 %sum3, i32* %result, align 4

  call void @putint(i32 %sum3)
  call void @putch(i32 10)

  ret i32 0
}