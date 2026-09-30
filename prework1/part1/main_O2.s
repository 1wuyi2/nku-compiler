	.file	"main.c"
	.text
	.p2align 4
	.globl	factorial
	.type	factorial, @function
factorial:
.LFB23:
	.cfi_startproc
	endbr64
	cmpl	$1, %edi
	jle	.L4
	leal	1(%rdi), %esi
	andl	$1, %edi
	movl	$1, %edx
	movl	$2, %eax
	jne	.L3
	movl	$3, %eax
	movl	$2, %edx
	cmpl	%esi, %eax
	je	.L1
	.p2align 4,,10
	.p2align 3
.L3:
	imull	%eax, %edx
	leal	1(%rax), %ecx
	addl	$2, %eax
	imull	%ecx, %edx
	cmpl	%esi, %eax
	jne	.L3
.L1:
	movl	%edx, %eax
	ret
	.p2align 4,,10
	.p2align 3
.L4:
	movl	$1, %edx
	movl	%edx, %eax
	ret
	.cfi_endproc
.LFE23:
	.size	factorial, .-factorial
	.section	.rodata.str1.1,"aMS",@progbits,1
.LC0:
	.string	"Input n: "
.LC1:
	.string	"%d"
.LC2:
	.string	"factorial = %d\n"
	.section	.text.startup,"ax",@progbits
	.p2align 4
	.globl	main
	.type	main, @function
main:
.LFB24:
	.cfi_startproc
	endbr64
	subq	$24, %rsp
	.cfi_def_cfa_offset 32
	leaq	.LC0(%rip), %rsi
	movl	$2, %edi
	movq	%fs:40, %rax
	movq	%rax, 8(%rsp)
	xorl	%eax, %eax
	call	__printf_chk@PLT
	leaq	4(%rsp), %rsi
	leaq	.LC1(%rip), %rdi
	xorl	%eax, %eax
	call	__isoc99_scanf@PLT
	movl	4(%rsp), %ecx
	cmpl	$1, %ecx
	jle	.L16
	leal	1(%rcx), %esi
	andl	$1, %ecx
	movl	$1, %edx
	movl	$2, %eax
	jne	.L14
	movl	$3, %eax
	movl	$2, %edx
	cmpl	%esi, %eax
	je	.L13
	.p2align 4,,10
	.p2align 3
.L14:
	imull	%eax, %edx
	leal	1(%rax), %ecx
	addl	$2, %eax
	imull	%ecx, %edx
	cmpl	%esi, %eax
	jne	.L14
.L13:
	xorl	%eax, %eax
	leaq	.LC2(%rip), %rsi
	movl	$2, %edi
	call	__printf_chk@PLT
	movq	8(%rsp), %rax
	subq	%fs:40, %rax
	jne	.L24
	xorl	%eax, %eax
	addq	$24, %rsp
	.cfi_remember_state
	.cfi_def_cfa_offset 8
	ret
.L16:
	.cfi_restore_state
	movl	$1, %edx
	jmp	.L13
.L24:
	call	__stack_chk_fail@PLT
	.cfi_endproc
.LFE24:
	.size	main, .-main
	.ident	"GCC: (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0"
	.section	.note.GNU-stack,"",@progbits
	.section	.note.gnu.property,"a"
	.align 8
	.long	1f - 0f
	.long	4f - 1f
	.long	5
0:
	.string	"GNU"
1:
	.align 8
	.long	0xc0000002
	.long	3f - 2f
2:
	.long	0x3
3:
	.align 8
4:
