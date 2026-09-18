/*
 * rtcore.ld - linker script of the pTOS real-time core runtime
 *
 * Flash from RTX_IMAGE_ADDR, RAM in core 1's share of the SRAM, after the
 * mailbox; the stack at the very top (SRAM8/SRAM9).  See rtx_rp2350.h.
 */

MEMORY
{
    flash : ORIGIN = 0x10800000, LENGTH = 1M
    ram   : ORIGIN = 0x20072100, LENGTH = 0x20080000 - 0x20072100
}

__stack_top = 0x20082000;

ENTRY(reset)

SECTIONS
{
    .text :
    {
        KEEP(*(.rtx_header))
        KEEP(*(.vectors))
        *(.text .text.* .rodata .rodata.*)
    } > flash

    .data :
    {
        __data_start = .;
        *(.data .data.*)
        . = ALIGN(4);
        __data_end = .;
    } > ram AT > flash
    __data_load = LOADADDR(.data);

    .bss (NOLOAD) :
    {
        __bss_start = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > ram

    /DISCARD/ : { *(.ARM.exidx*) *(.comment) }
}
