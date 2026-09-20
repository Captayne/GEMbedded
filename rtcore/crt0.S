/*
 * crt0.S - start-up of the pTOS real-time core runtime (RP2350, core 1)
 *
 * pTOS finds struct rtx_image at the very start of this image
 * (RTX_IMAGE_ADDR) and launches core 1 with the vector table, stack
 * pointer and entry point recorded there.
 *
 * Only the header, a boot vector table and the copy loop below run from
 * flash.  The copy loop moves the rest of the runtime into core 1's SRAM
 * (see rtcore.ld) and continues there, so that nothing core 1 does ever
 * waits for the flash.
 */

#include "rtx_rp2350.h"

        .syntax unified
        .thumb

// ==== image header (struct rtx_image) ======================================

        .section .rtx_header, "a"
        .word   RTX_IMAGE_MAGIC
        .word   1                       // version
        .word   boot_vectors
        .word   __stack_top
        .word   reset + 1               // Thumb
name:   .ascii  "IRKernel real-time core"
        .space  44 - (. - name)

// ==== boot vector table (flash), in use until the copy is done ============

        .section .boot, "ax"
        .balign 512
boot_vectors:
        .word   __stack_top
        .word   reset + 1
        .rept   66
        .word   boot_fault + 1
        .endr

        .balign 4
        .type   boot_fault, %function
boot_fault:
        b       boot_fault

// ==== reset (flash): copy the runtime into SRAM and start it there ========

        .balign 4
        .globl  reset
        .type   reset, %function
reset:
        // vector table, code, read-only and initialised data
        ldr     r0, =__ram_start
        ldr     r1, =__ram_end
        ldr     r2, =__ram_load
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
        ldr     r0, =vectors
        ldr     r1, =0xe000ed08         // VTOR (core 1's own)
        str     r0, [r1]
        dsb
        isb
#if defined(__ARM_FP) && (__ARM_FP > 0)
        // CPACR belongs to this core alone: what pTOS enabled on core 0
        // does nothing for us here, and IRKernel saves s16..s31 at every
        // context switch.  Without this the first switch faults.
        ldr     r0, =0xe000ed88         // CPACR (core 1's own)
        ldr     r1, [r0]
        orr     r1, r1, #(0xF << 20)    // CP10 and CP11, full access
        str     r1, [r0]
        dsb
        isb
#endif

        // main is in SRAM, out of reach of a bl from flash
        ldr     r0, =main
        blx     r0
5:      b       5b
        .ltorg

// ==== vector table (SRAM) ==================================================
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

// ==== faults ================================================================
// Record where it happened in the mailbox, withdraw the runtime (pTOS then
// answers every call with RTX_E_NOCORE) and stop.

        .text
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
