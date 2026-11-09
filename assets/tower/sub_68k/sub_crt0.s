| ==============================================================================
| Sega CD Sub-CPU 68000 Startup Routine (Tower of Power Edition)
| Sets vector table, initializes Sub-CPU PRG RAM, and branches to sub_main()
|
| SPDX-License-Identifier: MIT
| ==============================================================================

    .section .vectors, "ax"
    .global _sub_start

    | Vector table
    .long   0x00080000      | Initial Stack Pointer (Top of 512 KB PRG RAM)
    .long   _sub_start      | Initial Program Counter
    .long   _sub_err        | Bus Error
    .long   _sub_err        | Address Error
    .long   _sub_err        | Illegal Instruction
    .long   _sub_err        | Zero Divide
    .long   _sub_err        | CHK Instruction
    .long   _sub_err        | TRAPV Instruction
    .long   _sub_err        | Privilege Violation
    .long   _sub_err        | Trace
    .long   _sub_err        | Line 1010 Emulator
    .long   _sub_err        | Line 1111 Emulator
    .space  4 * 12          | Reserved vectors
    .long   _sub_spurious   | Spurious Interrupt
    .long   _sub_irq1       | Level 1 (Graphics / Word RAM)
    .long   _sub_irq2       | Level 2 (CD-ROM Drive)
    .long   _sub_irq3       | Level 3 (Timer)
    .long   _sub_irq4       | Level 4 (Ricoh PCM)
    .long   _sub_irq5       | Level 5 (Reserved)
    .long   _sub_irq6       | Level 6 (Reserved)
    .long   _sub_irq7       | Level 7 (Non-Maskable Interrupt)

    .text
    .even
_sub_start:
    | Disable interrupts during boot
    move.w  #0x2700, %sr

    | Clear .bss in PRG RAM
    lea     __bss_start, %a0
    lea     __bss_end, %a1
_zero_bss:
    cmpa.l  %a1, %a0
    jge     _zero_bss_done
    clr.l   (%a0)+
    jra     _zero_bss
_zero_bss_done:

    | Enable Sub-CPU interrupts (Level 2/3 unmasked)
    move.w  #0x2000, %sr

    | Jump to Sub-CPU C main entry
    jsr     sub_main

_sub_halt:
    jra     _sub_halt

_sub_irq1:
_sub_irq2:
_sub_irq3:
_sub_irq4:
_sub_irq5:
_sub_irq6:
_sub_irq7:
_sub_spurious:
_sub_err:
    rte
