/*
 * crt0.S - start-up of the pTOS real-time core runtime (RP2350, core 1)
 *
 * pTOS finds struct rtx_image at the very start of this image
 * (RTX_IMAGE_ADDR) and launches core 1 with the vector table, stack
 * pointer and entry point recorded there.
 */

#include "rtx_rp2350.h"

        .syntax unified
        .thumb

// ==== image header (struct rtx_image) ======================================

        .section .rtx_header, "a"
        .word   RTX_IMAGE_MAGIC
        .word   1                       // version
        .word   vectors
        .word   __stack_top
        .word   reset + 1               // Thumb
name:   .ascii  "IRKernel real-time core"
        .space  44 - (. - name)

// ==== vector table ==========================================================
// Core 1 uses no interrupts yet; every exception is a fault.

        .section .vectors, "a"
        .balign 512
vectors:
        .word   __stack_top
        .word   reset + 1
        .rept   14
        .word   fault + 1
        .endr
        .rept   52
        .word   fault + 1
        .endr

// ==== reset =================================================================

        .text
        .balign 4
        .globl  reset
        .type   reset, %function
reset:
        ldr     r0, =vectors
        ldr     r1, =0xe000ed08         // VTOR (core 1's own)
        str     r0, [r1]

        // .data from flash to RAM
        ldr     r0, =__data_start
        ldr     r1, =__data_end
        ldr     r2, =__data_load
1:      cmp     r0, r1
        bhs     2f
        ldr     r3, [r2], #4
        str     r3, [r0], #4
        b       1b
2:
        // clear .bss (the mailbox below it belongs to pTOS as well)
        ldr     r0, =__bss_start
        ldr     r1, =__bss_end
        mov     r2, #0
3:      cmp     r0, r1
        bhs     4f
        str     r2, [r0], #4
        b       3b
4:
        bl      main
5:      b       5b

// ==== faults ================================================================
// Record where it happened in the mailbox, withdraw the runtime (pTOS then
// answers every call with RTX_E_NOCORE) and stop.

        .balign 4
        .type   fault, %function
fault:
        cpsid   i
        tst     lr, #4
        ite     eq
        mrseq   r0, msp
        mrsne   r0, psp
        ldr     r1, =RTX_MAILBOX_ADDR
        mov     r2, #0
        str     r2, [r1, #0]            // magic = 0: runtime gone
        mrs     r2, ipsr
        str     r2, [r1, #64]           // fault[0]: exception number
        ldr     r2, [r0, #24]
        str     r2, [r1, #68]           // fault[1]: pc
        ldr     r2, [r0, #20]
        str     r2, [r1, #72]           // fault[2]: lr
        ldr     r2, =0xe000ed28
        ldr     r2, [r2]
        str     r2, [r1, #76]           // fault[3]: CFSR
6:      wfi
        b       6b
