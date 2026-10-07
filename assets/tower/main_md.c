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

int main(bool hardReset)
{
    (void)hardReset;

    /* Initialize Genesis VDP and text plane */
    VDP_init();
    VDP_drawText("SEGA TOWER OF POWER DEV", 8, 2);
    VDP_drawText("-----------------------", 8, 3);

    /* Initialize Tower Hardware Stack */
    VDP_drawText("Initializing hardware...", 8, 5);
    uint16_t hw_flags = tower_init();

    /* Display detected hardware configuration */
    VDP_drawText("[x] Mega Drive (68000 @ 7.67 MHz)", 4, 7);

    if (hw_flags & TOWER_FLAG_32X)
        VDP_drawText("[x] Sega 32X (Dual SH-2 @ 23 MHz)", 4, 9);
    else
        VDP_drawText("[ ] Sega 32X: Not Attached", 4, 9);

    if (hw_flags & TOWER_FLAG_CD)
        VDP_drawText("[x] Sega CD (Sub-68K @ 12.5 MHz)", 4, 11);
    else
        VDP_drawText("[ ] Sega CD: Not Attached", 4, 11);

    if (hw_flags & TOWER_FLAG_EVERDRIVE)
        VDP_drawText("[x] Mega EverDrive (EDIO Active)", 4, 13);
    else
        VDP_drawText("[ ] Mega EverDrive: Standard Cart", 4, 13);

    VDP_drawText("Press D-Pad to trigger workloads", 4, 16);

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
            tower_send_cd_cmd(0x0002, (uint16_t)(frame_count & 0xFF), 0);
            tower_swap_word_ram();
        }

        /* 4. Telemetry logging via EverDrive USB */
        if ((hw_flags & TOWER_FLAG_EVERDRIVE) && ((frame_count & 0x3F) == 0))
        {
            tower_log(">> [TOWER] Frame Tick 60Hz OK\n");
        }

        /* 5. Update Genesis On-Screen Frame Counter */
        if ((frame_count % 30) == 0)
        {
            sprintf(text_buf, "Frame: %lu", (unsigned long)frame_count);
            VDP_drawText(text_buf, 8, 19);
        }

        /* Synchronize Genesis VBlank */
        SYS_doVBlankProcess();
    }

    return 0;
}
