/*
 * mars.ld - SH-2 Linker Script for Sega 32X (Tower of Power)
 * Supports dual Master/Slave SH-2 execution from SDRAM and ROM.
 *
 * SPDX-License-Identifier: MIT
 */

OUTPUT_ARCH(sh)
EXTERN (_start)
ENTRY (_start)

MEMORY
{
    rom (rx)   : ORIGIN = 0x02000000, LENGTH = 4M       /* Cartridge ROM Window */
    sdram (rwx): ORIGIN = 0x06000000, LENGTH = 0x3F800  /* SDRAM (Master region) */
}

PROVIDE (__stack = 0x0603F800);
PROVIDE (__slave_stack = 0x06040000);

SECTIONS
{
    .text : {
        *(.header)
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
        __text_end = .;
    } > rom

    .data : AT(__text_end) {
        __data_start = .;
        *(.data*)
        . = ALIGN(4);
        __data_end = .;
    } > sdram

    .bss : {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > sdram
}
