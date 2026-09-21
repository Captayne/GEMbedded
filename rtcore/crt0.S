/*
 * crt0.S - start-up of the kernel (RP2350)
 *
 * This image boots.  It sits at the start of flash, where the bootrom
 * looks for an IMAGE_DEF block, and does two things on core 0:
 *
 *   1. copies itself into its share of the SRAM (see rtcore.ld) -- the
 *      vector table, code, read-only and initialised data -- and clears
 *      its .bss, so that nothing it does later waits for the flash;
 *   2. enters pTOS through the vector table at PTOS_IMAGE_ADDR (boot.c).
 *
 * Core 1 starts when pTOS calls start_core1() through the kernel's call
 * table, whose address is in struct rtx_image.
 *
 * Only the boot vector table, the IMAGE_DEF block, struct rtx_image and
 * the copy loop run from flash.
 */

#include "rtx_rp2350.h"

        .syntax unified
        .thumb

// ==== boot vector table (flash, 0x10000000) ===============================
// The bootrom takes the initial stack pointer and the reset handler from
// here.  Nothing else is ever taken: interrupts stay off until pTOS runs.

        .section .boot_vectors, "a"
boot_vectors:
        .word   __boot_stack
        .word   reset + 1
        .rept   66
        .word   boot_fault + 1
        .endr

// ==== IMAGE_DEF block ======================================================
// A single-item block marking this as an ARM Secure executable for the
// RP2350 (datasheet, "Binary blocks").  Its link offset of 0 makes it a
// loop of one block.  Must be within the first 4 KB of flash.

        .balign 4
        .word   0xffffded3              // PICOBIN_BLOCK_MARKER_START
        .byte   0x42                    // PICOBIN_BLOCK_ITEM_1BS_IMAGE_TYPE
        .byte   1                       // size in words
        .hword  0x1021                  // EXE | Secure | ARM | RP2350
        .byte   0xff                    // PICOBIN_BLOCK_ITEM_2BS_LAST
        .hword  1                       // size of the items above, in words
        .byte   0
        .word   0                       // link to the next block: itself
        .word   0xab123579              // PICOBIN_BLOCK_MARKER_END

// ==== image header (struct rtx_image, at RTX_IMAGE_ADDR) ==================

        .section .rtx_header, "a"
        .word   RTX_IMAGE_MAGIC
        .word   2                       // version
name:   .ascii  "IRKernel"
        .space  44 - (. - name)
        .word   kernel_api

// ==== reset (flash, core 0) ================================================

        .section .boot, "ax"
        .balign 4
        .type   boot_fault, %function
boot_fault:
        b       boot_fault

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
        // clear .bss (the mailbox below it is filled in by core 1)
        ldr     r0, =__bss_start
        ldr     r1, =__bss_end
        mov     r2, #0
3:      cmp     r0, r1
        bhs     4f
        str     r2, [r0], #4
        b       3b
4:
        // boot.c is in SRAM, out of reach of a bl from flash; it enters
        // pTOS and never returns
        ldr     r0, =boot_core0
        blx     r0
5:      b       5b
        .ltorg

// ==== core 1 (SRAM) ========================================================
// The bootrom has set VTOR and the stack pointer and jumped here.

        .text
        .balign 4
        .globl  core1_entry
        .type   core1_entry, %function
core1_entry:
        mov     r0, #0                  // no stack limit left over from
        msr     msplim, r0              // the bootrom
#if defined(__ARM_FP) && (__ARM_FP > 0)
        // CPACR belongs to this core alone: what pTOS enables on core 0
        // does nothing for us here, and IRKernel saves s16..s31 at every
        // context switch.  Without this the first switch faults.
        ldr     r0, =0xe000ed88         // CPACR (core 1's own)
        ldr     r1, [r0]
        orr     r1, r1, #(0xF << 20)    // CP10 and CP11, full access
        str     r1, [r0]
        dsb
        isb
#endif
        bl      main
6:      b       6b
        .ltorg

// ==== vector table (SRAM) ==================================================
// Core 1 uses no interrupts yet; every exception is a fault.

        .section .vectors, "a"
        .balign 512
        .globl  vectors
vectors:
        .word   __stack_top
        .word   core1_entry + 1
        .rept   66
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
7:      wfi
        b       7b
        .ltorg

// ==== core 0's stack while it boots ========================================
// Used by boot_core0() only; pTOS sets its own before it does anything.

        .bss
        .balign 8
        .space  512
__boot_stack:
