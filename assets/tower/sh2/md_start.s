/*
 * md_start.s - Genesis 68000 boot stub for the 32X cartridge ROM.
 *
 * Embedded at ROM offset 0x200 by mars_header.s. Everything here is
 * position independent (no references to its own labels except via bra/dbra)
 * because it is assembled to a raw binary without a linker script.
 *
 * Enables the 32X adapter, releases SH-2 resets, waits for Master ('MOK ')
 * and Slave ('SOK ') SH-2s, sends the 'GO! ' dispatch word, and clears COMM4.
 *
 * SPDX-License-Identifier: MIT
 */

        .text

/* Offset 0: every 68000 vector except reset points here. */
default_handler:
        rte

/* Offset 2: reset vector target. */
entry:
        move.w  #0x2700,%sr

        /* ADEN: switch the adapter on, then give it time before releasing the SH-2s. */
        move.w  #0x0001,0xA15100
        move.w  #0x3FFF,%d0
1:      dbra    %d0,1b

        /* ADEN | nRES: release both SH-2 resets. */
        move.w  #0x0003,0xA15100

        /* Wait for Master ('MOK ' high word) in COMM0 and Slave ('SOK ') in COMM2. */
2:      cmpi.w  #0x4D4F,0xA15120
        bne.s   2b
3:      cmpi.w  #0x534F,0xA15124
        bne.s   3b

        /* 'GO! ' into COMM4/COMM5 releases both SH-2 busy-wait loops. */
        move.w  #0x474F,0xA15128
        move.w  #0x2120,0xA1512A

        /* Wait for BOTH SH-2 cores to acknowledge by clearing COMM0 and COMM2 */
4:      tst.w   0xA15120
        bne.s   4b
5:      tst.w   0xA15124
        bne.s   5b

        /* Now safe to clear COMM4/COMM5: both cores have definitively started */
        move.w  #0x0000,0xA15128
        move.w  #0x0000,0xA1512A

        /* Nothing left for the 68000 to do; the SH-2s own the demo from here. */
6:      bra.s   6b
