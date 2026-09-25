@ accstart.S - startup code for pTOS programs that can be desk accessories
@
@ Copyright (C) 2026 Andreas Keibel
@
@ libcmini's startup code always runs as an application (it shrinks its
@ memory with Mshrink(), which an accessory must not do).  This one only
@ switches to a stack in the program's own BSS -- an accessory is started
@ with its stack pointer at the end of a TPA that the AES has already
@ shrunk away -- and calls
@
@       void start_main(BASEPAGE *bp);
@
@ which decides between accessory (bp->p_parent == NULL) and application
@ and shrinks the memory itself in the latter case.

	.syntax unified
	.thumb

	.globl	_start
	.globl	_base
	.extern	start_main

	.text
	.balign	4
	.type	_start, %function
_start:
	ldr	r1, =_base
	str	r0, [r1]		@ basepage, also for libcmini
	ldr	r1, =stack_top
	mov	sp, r1
	bl	start_main
1:	b	1b			@ start_main() does not return

	.bss
	.balign	8
	.space	4096
stack_top:
