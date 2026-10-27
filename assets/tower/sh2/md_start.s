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

        /* TMSS check and unlock */
        move.b  0xA10001,%d0
        andi.b  #0x0F,%d0
        beq.s   0f
        move.l  #0x53454741,0xA14000
0:
        /* Mute all 4 PSG channels to eliminate high-pitched audio squeal */
        move.b  #0x9F,0xC00011
        move.b  #0xBF,0xC00011
        move.b  #0xDF,0xC00011
        move.b  #0xFF,0xC00011

        /* Initialize Genesis VDP registers (enable display + 320x224 40-cell mode) */
        lea     0xC00004,%a0
        lea     10f(%pc),%a1
        moveq   #18,%d0
11:     move.w  (%a1)+,(%a0)
        dbra    %d0,11b

        /* ADEN: switch the adapter on, then give it time before releasing the SH-2s. */
        move.w  #0x0001,0xA15100
        move.w  #0x3FFF,%d0
1:      dbra    %d0,1b

        /* ADEN | nRES | FM (0x8003): release SH-2s and grant SH-2 ownership of 32X VDP */
        move.w  #0x8003,0xA15100

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

        /* Initial 19 Genesis VDP register words */
10:     .word   0x8004  /* Reg 0: Normal display */
        .word   0x8174  /* Reg 1: Enable display, V-Int, DMA, 224-line */
        .word   0x8238  /* Reg 2: Plane A = $E000 */
        .word   0x8334  /* Reg 3: Window  = $D000 */
        .word   0x8406  /* Reg 4: Plane B = $C000 */
        .word   0x857A  /* Reg 5: Sprite  = $F400 */
        .word   0x8600  /* Reg 6: unused */
        .word   0x8700  /* Reg 7: Background color = 0 */
        .word   0x8800  /* Reg 8: unused */
        .word   0x8900  /* Reg 9: unused */
        .word   0x8A01  /* Reg 10: H-Int timing */
        .word   0x8B00  /* Reg 11: Scroll mode */
        .word   0x8C81  /* Reg 12: 40-cell (320-pixel) mode */
        .word   0x8D3C  /* Reg 13: HScroll = $F000 */
        .word   0x8E00  /* Reg 14: unused */
        .word   0x8F02  /* Reg 15: Auto-increment 2 */
        .word   0x9001  /* Reg 16: Scroll size 32x64 */
        .word   0x9100  /* Reg 17: Window H */
        .word   0x9200  /* Reg 18: Window V */
