/*
 * rtcore.ld - linker script of the pTOS real-time core runtime
 *
 * The image is stored at the start of flash, where the bootrom finds it,
 * but only its boot vectors, header and a start-up stub run from there: everything else -- vector table,
 * code, read-only and initialised data -- is copied into core 1's share of
 * the SRAM at start-up and runs from there.  Core 1 then does not depend
 * on the flash at all: flash being written (which stops execution from
 * it) or the shared XIP cache being churned by core 0 cannot delay it.
 *
 * SRAM: the mailbox at RTX_RAM_BASE, the runtime from +0x200 (the vector
 * table needs 512-byte alignment), the stack at the very top.  See
 * rtx_rp2350.h.
 */

MEMORY
{
    flash : ORIGIN = 0x10000000, LENGTH = 64K     /* pTOS follows */
    ram   : ORIGIN = 0x20072200, LENGTH = 0x20080000 - 0x20072200
}

__stack_top = 0x20082000;

ENTRY(reset)

SECTIONS
{
    /* runs from flash: the boot vector table and IMAGE_DEF, which the
       bootrom wants at the very start; the header at RTX_IMAGE_ADDR; the
       copy loop */
    .boot :
    {
        KEEP(*(.boot_vectors))
        . = 0x400;
        KEEP(*(.rtx_header))
        KEEP(*(.boot))
    } > flash

    /* stored in flash, runs from SRAM */
    .ram :
    {
        __ram_start = .;
        KEEP(*(.vectors))
        *(.text .text.* .rodata .rodata.*)
        *(.data .data.*)
        . = ALIGN(4);
        __ram_end = .;
    } > ram AT > flash
    __ram_load = LOADADDR(.ram);

    .bss (NOLOAD) :
    {
        __bss_start = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > ram

    /DISCARD/ : { *(.ARM.exidx*) *(.comment) }
}
