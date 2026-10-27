/**
 * @file main_md.c
 * @brief Sega "Tower of Power" Master Coordinator (Genesis 68000 / SGDK).
 *
 * Coordinates:
 *   - Genesis 68000: VDP display planes, joypad polling, SGDK runtime
 *   - 32X Dual SH-2: Direct-color framebuffer, 3D math & PWM audio
 *   - Sega CD Sub-68000: Word RAM 1M mode streaming & Ricoh PCM audio
 *   - Mega EverDrive: Live USB console logging & SD streaming
 *
 * SPDX-License-Identifier: MIT
 */

#include <genesis.h>
#include "boot/tower.h"
#include "everdrive/everdrive.h"

/**
 * @brief Configures distinct VDP color palettes for the Tower of Power display.
 */
static void init_palettes(void)
{
    /* Backdrop: Deep navy blue */
    PAL_setColor(0,       RGB24_TO_VDPCOLOR(0x0b112c));

    /* PAL0: Dedicated status colors and primary UI text */
    PAL_setColor(1,       RGB24_TO_VDPCOLOR(0x00ff88)); /* Active hardware green */
    PAL_setColor(2,       RGB24_TO_VDPCOLOR(0xfbbf24)); /* Amber gold */
    PAL_setColor(3,       RGB24_TO_VDPCOLOR(0x38bdf8)); /* Cyan accent */
    PAL_setColor(15,      RGB24_TO_VDPCOLOR(0xffffff)); /* Pure white text */

    /* PAL1..3: Status banners and diagnostic accents */
    PAL_setColor(16 + 15, RGB24_TO_VDPCOLOR(0xfbbf24)); /* PAL1 text (amber) */
    PAL_setColor(32 + 15, RGB24_TO_VDPCOLOR(0x38bdf8)); /* PAL2 text (cyan) */
    PAL_setColor(48 + 15, RGB24_TO_VDPCOLOR(0xf43f5e)); /* PAL3 text (ruby) */
}

int main(bool hardReset)
{
    (void)hardReset;

    /* Initialize Genesis VDP and text plane */
    VDP_init();
    init_palettes();

    VDP_setTextPalette(PAL0);
    VDP_drawText("========================================", 0, 1);
    VDP_setTextPalette(PAL1);
    VDP_drawText("     SEGA TOWER OF POWER ARCHITECTURE   ", 0, 2);
    VDP_setTextPalette(PAL0);
    VDP_drawText("----------------------------------------", 0, 3);

    /* Initialize Tower Hardware Stack */
    VDP_setTextPalette(PAL2);
    VDP_drawText("Initializing hardware stack...", 4, 5);
    uint16_t hw_flags = tower_init();

    /* Display detected hardware configuration */
    VDP_setTextPalette(PAL0);
    VDP_drawText("[x] Mega Drive (68000 @ 7.67 MHz)", 4, 7);

    if (hw_flags & TOWER_FLAG_32X)
    {
        VDP_setTextPalette(PAL0);
        VDP_drawText("[x] Sega 32X (Dual SH-2 @ 23 MHz)", 4, 9);
    }
    else
    {
        VDP_setTextPalette(PAL3);
        VDP_drawText("[ ] Sega 32X: Standalone Mode", 4, 9);
    }

    if (hw_flags & TOWER_FLAG_CD)
    {
        VDP_setTextPalette(PAL0);
        VDP_drawText("[x] Sega CD (Sub-68K @ 12.5 MHz)", 4, 11);
    }
    else
    {
        VDP_setTextPalette(PAL3);
        VDP_drawText("[ ] Sega CD: Standalone Mode", 4, 11);
    }

    if (hw_flags & TOWER_FLAG_EVERDRIVE)
    {
        VDP_setTextPalette(PAL0);
        VDP_drawText("[x] Mega EverDrive (EDIO Active)", 4, 13);
    }
    else
    {
        VDP_setTextPalette(PAL2);
        VDP_drawText("[ ] Mega EverDrive: Cartridge Mode", 4, 13);
    }

    VDP_setTextPalette(PAL1);
    VDP_drawText("D-Pad: Pitch/Pan  Workloads: Active", 4, 16);

    uint32_t frame_count = 0;
    char text_buf[32];

    while (1)
    {
        frame_count++;

        /* 1. Poll Controller */
        uint16_t pad = JOY_readJoypad(JOY_1);

        /* 2. Dispatch commands to 32X SH-2 if present */
        if (hw_flags & TOWER_FLAG_32X)
        {
            tower_send_sh2_cmd(0x0001, (uint16_t)(frame_count & 0x1F), pad);
        }

        /* 3. Dispatch commands and swap Word RAM with Sega CD if present */
        if (hw_flags & TOWER_FLAG_CD)
        {
            tower_send_cd_cmd(0x0002, (uint16_t)(frame_count & 0xFF), pad);
            tower_swap_word_ram();
        }

        /* 4. Telemetry logging via EverDrive USB */
        if ((hw_flags & TOWER_FLAG_EVERDRIVE) && ((frame_count & 0x3F) == 0))
        {
            tower_log(">> [TOWER] Frame Tick 60Hz OK\n");
        }

        /* 5. Update Genesis On-Screen Real-Time 4-CPU Telemetry HUD */
        if ((frame_count % 8) == 0)
        {
            uint16_t sh2_frames = (hw_flags & TOWER_FLAG_32X) ? MARS_SYS_COMM7_W : 0;
            uint16_t sh2_pwm = (hw_flags & TOWER_FLAG_32X) ? MARS_SYS_COMM3_W : 0;
            uint16_t cd_frames = (hw_flags & TOWER_FLAG_CD) ? MCD_COM_STAT1 : 0;
            uint16_t cd_pitch = (hw_flags & TOWER_FLAG_CD) ? MCD_COM_STAT2 : 0;

            sprintf(text_buf, "MD:%-5lu 32X:%-5u PWM:%-5u",
                    (unsigned long)frame_count, sh2_frames, sh2_pwm);
            VDP_drawText(text_buf, 2, 19);

            sprintf(text_buf, "CD:%-5u PCM Pitch:0x%04X ", cd_frames, cd_pitch);
            VDP_drawText(text_buf, 2, 21);
        }

        /* Synchronize Genesis VBlank */
        SYS_doVBlankProcess();
    }

    return 0;
}
