/*
 * sub_68k.ld - Linker Script for Sega CD Sub-CPU 68000
 * Targets 512 KB Sub-CPU Program RAM (PRG-RAM) at address 0x00000000.
 *
 * Copyright (c) 2026 Dan Leslie
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
