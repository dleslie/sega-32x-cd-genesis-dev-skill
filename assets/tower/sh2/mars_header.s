! ==============================================================================
! Sega 32X cartridge header block (ROM offsets 0x000..0x3FF)
!
! 0x000  68000 vector table      0x100  Genesis/32X header
! 0x200  68000 boot stub         0x3C0  32X module header
! The SH-2 code (mars_crt0.s) begins at 0x400, i.e. 0x02000400 on the SH-2 bus.
!
! SPDX-License-Identifier: MIT
! ==============================================================================

        .section .header, "a"

        .global _mars_header

_mars_header:
        .long   0x00FF0000              ! Initial 68000 SP
        .long   0x00000202              ! Reset PC: 'entry' in md_start.s
        .rept   62
        .long   0x00000200              ! Every other vector: bare RTE
        .endr

        .org    0x100
        .ascii  "SEGA 32X        "
        .ascii  "(C)DLESLIE 2026 "
        .ascii  "32X TOWER DEMO                        "
        .org    0x150, 0x20
        .ascii  "32X TOWER DEMO                        "
        .org    0x180, 0x20
        .ascii  "GM 00000000-00"
        .org    0x18E
        .word   0                       ! Checksum
        .ascii  "J               "
        .long   0x00000000              ! ROM start
        .long   0x0007FFFF              ! ROM end
        .long   0x00FF0000              ! RAM start
        .long   0x00FFFFFF              ! RAM end
        .org    0x1F0, 0x20
        .ascii  "U               "

        .org    0x200
        .incbin "md_start.bin"

        .org    0x3C0
        .ascii  "32X TOWER DEMO  "      ! Module name (16 bytes)
        .long   0                       ! 0x3D0 version
        .long   0                       ! 0x3D4 copy source (unused: size is 0)
        .long   0                       ! 0x3D8 copy destination
        .long   0                       ! 0x3DC copy size; 0 = run in place from ROM
        .long   _sh2_master_entry       ! 0x3E0 Master SH-2 PC
        .long   _sh2_slave_entry        ! 0x3E4 Slave SH-2 PC
        .long   _start                  ! 0x3E8 Master VBR
        .long   _start                  ! 0x3EC Slave VBR

        .org    0x400
