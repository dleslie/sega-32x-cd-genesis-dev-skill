! ==============================================================================
! Sega 32X Dual SH-2 Startup Routine (Tower of Power Edition)
! Master & Slave SH-2 Entry, Vector Setup, SDRAM Init, and Genesis Handshake
!
! Copyright (c) 2026 Joseph Groff, Victor Luchits, Dan Leslie
! SPDX-License-Identifier: MIT
! ==============================================================================

        .section .header, "ax"
        .global _start
        .global _sh2_master_entry
        .global _sh2_slave_entry

_start:
! Vector Base Table
        .long   _sh2_master_entry       ! Reset PC (Master)
        .long   0x0603F800              ! Reset SP (Master)
        .long   _sh2_slave_entry        ! Reset PC (Slave)
        .long   0x06040000              ! Reset SP (Slave)

! Master SH-2 Entry Point
        .align 4
_sh2_master_entry:
        ! Set VBR to beginning of vector table
        mov.l   vbr_master_addr, r0
        ldc     r0, vbr

        ! Set Master Stack
        mov.l   stack_master_addr, r15

        ! Enable Cache via CCR (0xFFFFFE92)
        mov.l   ccr_addr, r1
        mov.l   ccr_enable, r0
        mov.b   r0, @r1

        ! Copy .data section from ROM to SDRAM
        mov.l   data_start_addr, r1
        mov.l   data_end_addr, r2
        mov.l   data_load_addr, r3
copy_data_loop:
        cmp/hs  r2, r1
        bt      copy_data_done
        mov.l   @r3+, r0
        mov.l   r0, @r1
        add     #4, r1
        bra     copy_data_loop
        nop
copy_data_done:

        ! Zero .bss section in SDRAM
        mov.l   bss_start_addr, r1
        mov.l   bss_end_addr, r2
        mov     #0, r0
zero_bss_loop:
        cmp/hs  r2, r1
        bt      zero_bss_done
        mov.l   r0, @r1
        add     #4, r1
        bra     zero_bss_loop
        nop
zero_bss_done:

        ! Signal Genesis 68000: Master Ready ('MOK ') in COMM0 (0x20004020)
        mov.l   comm0_addr, r1
        mov.l   magic_mok, r0
        mov.l   r0, @r1

        ! Wait for Genesis Go signal ('GO! ') in COMM4 (0x20004028)
        mov.l   comm4_addr, r2
        mov.l   magic_go, r3
wait_genesis_master:
        mov.l   @r2, r0
        cmp/eq  r3, r0
        bf      wait_genesis_master

        ! Jump to Master SH-2 C entry point
        mov.l   master_main_addr, r0
        jsr     @r0
        nop

master_halt:
        bra     master_halt
        nop

! Slave SH-2 Entry Point
        .align 4
_sh2_slave_entry:
        ! Set VBR
        mov.l   vbr_slave_addr, r0
        ldc     r0, vbr

        ! Set Slave Stack
        mov.l   stack_slave_addr, r15

        ! Enable Cache
        mov.l   ccr_addr, r1
        mov.l   ccr_enable, r0
        mov.b   r0, @r1

        ! Signal Genesis 68000: Slave Ready ('SOK ') in COMM2 (0x20004024)
        mov.l   comm2_addr, r1
        mov.l   magic_sok, r0
        mov.l   r0, @r1

        ! Wait for Genesis Go signal in COMM4
        mov.l   comm4_addr, r2
        mov.l   magic_go, r3
wait_genesis_slave:
        mov.l   @r2, r0
        cmp/eq  r3, r0
        bf      wait_genesis_slave

        ! Jump to Slave SH-2 C entry point
        mov.l   slave_main_addr, r0
        jsr     @r0
        nop

slave_halt:
        bra     slave_halt
        nop

! Constants & Literal Pool
        .align 4
vbr_master_addr:    .long   _start
vbr_slave_addr:     .long   _start
stack_master_addr:  .long   0x0603F800
stack_slave_addr:   .long   0x06040000
ccr_addr:           .long   0xFFFFFE92
ccr_enable:         .long   0x00000001
comm0_addr:         .long   0x20004020
comm2_addr:         .long   0x20004024
comm4_addr:         .long   0x20004028
magic_mok:          .long   0x4D4F4B20   ! 'MOK '
magic_sok:          .long   0x534F4B20   ! 'SOK '
magic_go:           .long   0x474F2120   ! 'GO! '

data_start_addr:    .long   __data_start
data_end_addr:      .long   __data_end
data_load_addr:     .long   __text_end
bss_start_addr:     .long   __bss_start
bss_end_addr:       .long   __bss_end

master_main_addr:   .long   _mars_master_main
slave_main_addr:    .long   _mars_slave_main
