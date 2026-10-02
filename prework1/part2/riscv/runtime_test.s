    .text
    .globl main

main:
    addi sp, sp, -16
    sd ra, 8(sp)

    li a0, 123
    call putint

    li a0, 10
    call putch

    ld ra, 8(sp)
    addi sp, sp, 16

    li a0, 0
    ret
