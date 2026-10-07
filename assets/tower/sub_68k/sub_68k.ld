/*
 * sub_68k.ld - Linker Script for Sega CD Sub-CPU (Tower of Power)
 * Executes inside 512 KB Sub-CPU Program RAM (PRG-RAM).
 *
 * SPDX-License-Identifier: MIT
 */

OUTPUT_ARCH(m68k)
ENTRY(_sub_start)

MEMORY
{
    prg_ram (rwx) : ORIGIN = 0x00000000, LENGTH = 512K
}

PROVIDE (__stack = 0x00080000);

SECTIONS
{
    .vectors : {
        *(.vectors)
        . = ALIGN(4);
    } > prg_ram

    .text : {
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
        __text_end = .;
    } > prg_ram

    .data : {
        __data_start = .;
        *(.data*)
        . = ALIGN(4);
        __data_end = .;
    } > prg_ram

    .bss : {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > prg_ram
}
