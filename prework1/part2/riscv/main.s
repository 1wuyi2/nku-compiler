    .text

    # ============================================================
    # int calc(int n)
    # ============================================================
    .globl calc
    .type calc, @function

calc:
    # 48-byte stack frame
    #
    #  0(sp)  ~ 28(sp) : values[0] ~ values[7]
    # 32(sp)            : i
    # 36(sp)            : result
    # 40(sp)            : n
    #
    addi sp, sp, -48

    # Save n
    sw a0, 40(sp)

    # i = 0
    li t0, 0
    sw t0, 32(sp)

    # result = 0
    li t0, 0
    sw t0, 36(sp)


.Lcalc_loop_cond:
    # while (i < n)
    lw t0, 32(sp)
    lw t1, 40(sp)

    # Exit when i >= n
    bge t0, t1, .Lcalc_loop_end


.Lcalc_loop_body:
    # t0 = i
    lw t0, 32(sp)

    # t1 = i + 1
    addiw t1, t0, 1

    # Calculate address of values[i]
    # address = sp + i * 4
    slli t4, t0, 2
    add t5, sp, t4

    # (i + 1) % 2
    li t2, 2
    remw t3, t1, t2

    # If remainder != 0, go to else
    bne t3, zero, .Lcalc_else


.Lcalc_if:
    # values[i] = (i + 1) * (i + 1)
    mulw t4, t1, t1
    sw t4, 0(t5)

    j .Lcalc_if_end


.Lcalc_else:
    # values[i] = -(i + 1)
    subw t4, zero, t1
    sw t4, 0(t5)


.Lcalc_if_end:
    # result = result + values[i]
    lw t4, 0(t5)
    lw t6, 36(sp)
    addw t6, t6, t4
    sw t6, 36(sp)

    # i = i + 1
    lw t0, 32(sp)
    addiw t0, t0, 1
    sw t0, 32(sp)

    # Continue loop
    j .Lcalc_loop_cond


.Lcalc_loop_end:
    # return result
    lw a0, 36(sp)

    addi sp, sp, 48
    ret

    .size calc, .-calc


    # ============================================================
    # int main()
    # ============================================================
    .globl main
    .type main, @function

main:
    # 32-byte stack frame
    #
    #  0(sp)  : n
    #  4(sp)  : result
    #  8(sp)  : quotient
    # 12(sp)  : remainder
    # 24(sp)  : saved ra
    #
    addi sp, sp, -32
    sd ra, 24(sp)


    # ------------------------------------------------------------
    # n = getint()
    # ------------------------------------------------------------
    call getint
    sw a0, 0(sp)


    # ------------------------------------------------------------
    # if (n < 1)
    #     n = 1;
    # else if (n > 8)
    #     n = 8;
    # ------------------------------------------------------------

    lw t0, 0(sp)
    li t1, 1

    # If n >= 1, check upper bound
    bge t0, t1, .Lmain_check_upper

    # n = 1
    li t0, 1
    sw t0, 0(sp)
    j .Lmain_bounds_end


.Lmain_check_upper:
    lw t0, 0(sp)
    li t1, 8

    # If 8 >= n, n is already valid
    bge t1, t0, .Lmain_bounds_end

    # n = 8
    li t0, 8
    sw t0, 0(sp)


.Lmain_bounds_end:

    # ------------------------------------------------------------
    # result = calc(n)
    # ------------------------------------------------------------
    lw a0, 0(sp)
    call calc
    sw a0, 4(sp)


    # ------------------------------------------------------------
    # quotient = n / 2
    # ------------------------------------------------------------
    lw t0, 0(sp)
    li t1, 2

    divw t2, t0, t1
    sw t2, 8(sp)


    # ------------------------------------------------------------
    # remainder = n % 2
    # ------------------------------------------------------------
    remw t3, t0, t1
    sw t3, 12(sp)


    # ------------------------------------------------------------
    # result = result + quotient + remainder + BASE
    # BASE = 3
    # ------------------------------------------------------------
    lw t0, 4(sp)
    lw t1, 8(sp)
    addw t0, t0, t1

    lw t1, 12(sp)
    addw t0, t0, t1

    addiw t0, t0, 3
    sw t0, 4(sp)


    # ------------------------------------------------------------
    # putint(result)
    # ------------------------------------------------------------
    lw a0, 4(sp)
    call putint


    # ------------------------------------------------------------
    # putch(10)
    # ------------------------------------------------------------
    li a0, 10
    call putch


    # ------------------------------------------------------------
    # return 0
    # ------------------------------------------------------------
    ld ra, 24(sp)
    addi sp, sp, 32

    li a0, 0
    ret

    .size main, .-main