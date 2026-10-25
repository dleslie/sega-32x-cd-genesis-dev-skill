/* main.c — minimal SGDK entry point and game loop.
 *
 * Copyright (c) 2026 Stéphane Dallongeville & Haroldo de Oliveira Pinheiro
 * SPDX-License-Identifier: MIT
 *
 * The two rules that keep you off a black screen:
 *   1. Seed a palette (and draw something) before the first frame.
 *   2. Call SYS_doVBlankProcess() at the END of every loop iteration — it
 *      flushes the DMA queue, advances the sprite/scroll engines, and waits
 *      for vblank. (Old tutorials say VDP_waitVInt(); that is outdated.)
 */
#include <genesis.h>
/* #include "resources.h"   // generated from res/resources.res */

/* Run game logic at ~20 Hz on a 60 fps NTSC console: tick every 3rd frame.
   Render/animate every frame. Adjust STEP to your source game's logic rate. */
#define LOGIC_EVERY 3

static void update_logic(u16 joy)
{
    (void)joy;
    /* your platform-clean core update goes here */
}

int main(bool hardReset)
{
    (void)hardReset;

    JOY_init();
    SPR_init();                       /* only if you use the sprite engine */

    /* Seed distinct palettes: backdrop, border, accent, text */
    PAL_setColor(0,       RGB24_TO_VDPCOLOR(0x000044)); /* Navy backdrop */
    PAL_setColor(15,      RGB24_TO_VDPCOLOR(0x00FF66)); /* PAL0 text (bright green) */
    PAL_setColor(16 + 15, RGB24_TO_VDPCOLOR(0xFFCC00)); /* PAL1 text (gold) */
    PAL_setColor(32 + 15, RGB24_TO_VDPCOLOR(0x33AAFF)); /* PAL2 text (cyan) */
    PAL_setColor(48 + 15, RGB24_TO_VDPCOLOR(0xFF4444)); /* PAL3 text (red) */

    VDP_setTextPalette(PAL1);
    VDP_drawText("========================================", 0, 2);

    VDP_setTextPalette(PAL2);
    VDP_drawText("       SEGA GENESIS DEV SKILL OK        ", 0, 6);

    VDP_setTextPalette(PAL0);
    VDP_drawText("          PRESS START TO PLAY           ", 0, 10);

    VDP_setTextPalette(PAL1);
    VDP_drawText("========================================", 0, 14);

    u16 prev = 0;
    u16 tick = 0;
    u32 frame = 0;
    char buf[32];

    while (TRUE)
    {
        u16 joy = JOY_readJoypad(JOY_1);
        u16 pressed = joy & ~prev;    /* edge-detected buttons */
        prev = joy;
        (void)pressed;

        if (++tick >= LOGIC_EVERY)
        {
            tick = 0;
            update_logic(joy);
        }

        frame++;
        sprintf(buf, "FRAME: %06lu", (unsigned long)frame);
        VDP_setTextPalette(PAL2);
        VDP_drawText(buf, 13, 18);

        if (joy & BUTTON_START) {
            VDP_setTextPalette(PAL3);
            VDP_drawText(">> START PRESSED <<", 10, 20);
        } else if (joy & BUTTON_RIGHT) {
            VDP_setTextPalette(PAL0);
            VDP_drawText(">> RIGHT HELD    <<", 10, 20);
        }

        SPR_update();                 /* if using sprites */
        SYS_doVBlankProcess();        /* MUST be last each frame */
    }
    return 0;
}
