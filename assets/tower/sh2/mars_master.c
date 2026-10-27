/**
 * @file mars_master.c
 * @brief Sega 32X Master SH-2 Main Loop (Tower of Power Edition).
 *
 * Drives 32X VDP direct-color double-buffered framebuffer rendering,
 * coordination with Slave SH-2, and execution of commands from Genesis 68000.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdbool.h>
#include "sh2_ipc.h"
#include "sh2_test.h"

/* 32X Hardware Registers via SH-2 Cache-Through Aperture (0x20000000) */
#define MARS_COMM4_REG       (*(volatile uint16_t *)0x20004028)
#define MARS_COMM5_REG       (*(volatile uint16_t *)0x2000402A)
#define MARS_COMM6_REG       (*(volatile uint16_t *)0x2000402C)
#define MARS_COMM7_REG       (*(volatile uint16_t *)0x2000402E)

#define MARS_FB_LINE_TABLE_WORDS 256
#define MARS_DISP_LINES          200

static uint16_t s_frame_count = 0;

void mars_vdp_init(void)
{
    /* Mode 2: 320x224 Direct Color Mode (15bpp BGR) */
    MARS_VDP_MODE_REG = 0x0002;

    /* Initialize line table for both DRAM banks (bank 0 and bank 1) */
    for (int b = 0; b < 2; b++)
    {
        uint32_t base = 0x24000000 + (b * 0x20000);
        volatile uint16_t *fb = (volatile uint16_t *)(uintptr_t)base;
        for (int y = 0; y < 224; y++)
        {
            int line_idx = y < MARS_DISP_LINES ? y : (MARS_DISP_LINES - 1);
            fb[y] = (uint16_t)(MARS_FB_LINE_TABLE_WORDS + (line_idx * 320));
        }
    }
}

void mars_vdp_flip(void)
{
    /* Toggle Framebuffer Select (bit 0) */
    MARS_VDP_FS_REG ^= 0x0001;

    /* Wait for VBlank frame swap acknowledgment */
    while ((MARS_VDP_FS_REG & 0x0002) != 0)
    {
        __asm__ volatile ("nop");
    }
}

void mars_master_main(void)
{
    /* Initialize uncached test state and run multi-core IPC verification */
    g_test_state = sh2_test_get_state();
    sh2_test_init_state(g_test_state);
    sh2_test_master_run(g_test_state);

    mars_vdp_init();

    while (1)
    {
        /* Read Genesis dispatch command */
        uint16_t cmd = MARS_COMM4_REG;
        uint16_t arg = MARS_COMM5_REG;
        (void)cmd;

        /* Determine active backbuffer to draw into */
        uint16_t fs = MARS_VDP_FS_REG & 0x0001;
        uint32_t base = 0x24000000 + (((fs ^ 1) & 1) * 0x20000);
        volatile uint16_t *fb = (volatile uint16_t *)(uintptr_t)base;

        /* Clear background (dark slate blue) with priority bit (0x8000) set */
        for (int y = 0; y < MARS_DISP_LINES; y++)
        {
            int line_start = MARS_FB_LINE_TABLE_WORDS + (y * 320);
            for (int x = 0; x < 320; x++)
            {
                fb[line_start + x] = 0x8000 | 0x1084;
            }
        }

        /* Color banner with priority bit (0x8000) set */
        uint16_t color = (uint16_t)(0x8000 | ((s_frame_count & 0x1F) << 10) | ((arg & 0x1F) << 5) | 0x001F);

        /* Draw a vibrant banner across scanlines */
        for (int y = 32; y < 96; y++)
        {
            int line_start = MARS_FB_LINE_TABLE_WORDS + (y * 320);
            for (int x = 32; x < 288; x++)
            {
                fb[line_start + x] = color;
            }
        }

        s_frame_count++;
        MARS_COMM7_REG = s_frame_count; /* Report frame counter to Genesis */

        /* Flip framebuffer at VBlank */
        mars_vdp_flip();
    }
}
