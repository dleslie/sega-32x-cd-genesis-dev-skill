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

/* 32X Hardware Registers via SH-2 Cache-Through Aperture (0x20000000) */
#define MARS_VDP_MODE_REG    (*(volatile uint16_t *)0x20004180)
#define MARS_VDP_FS_REG      (*(volatile uint16_t *)0x2000418A)
#define MARS_COMM4_REG       (*(volatile uint16_t *)0x20004028)
#define MARS_COMM5_REG       (*(volatile uint16_t *)0x2000402A)
#define MARS_COMM6_REG       (*(volatile uint16_t *)0x2000402C)
#define MARS_COMM7_REG       (*(volatile uint16_t *)0x2000402E)

/* Framebuffer Base (Direct 15-bit RGB mode) */
#define MARS_FRAMEBUFFER     ((volatile uint16_t *)0x24000000)

static uint16_t s_frame_count = 0;

void mars_vdp_init(void)
{
    /* Mode: 320x224 Direct Color (15bpp BGR), Priority Over Genesis Planes */
    MARS_VDP_MODE_REG = 0x0001; /* 15-bit Direct Color Mode */
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
    mars_vdp_init();

    while (1)
    {
        /* Read Genesis dispatch command */
        uint16_t cmd = MARS_COMM4_REG;
        uint16_t arg = MARS_COMM5_REG;

        /* Clear / draw pattern in active framebuffer */
        volatile uint16_t *fb = MARS_FRAMEBUFFER;
        uint16_t color = (uint16_t)((s_frame_count & 0x1F) | ((arg & 0x1F) << 5));

        /* Draw a small color-block bar across scanlines to verify rendering */
        for (int y = 32; y < 48; y++)
        {
            for (int x = 32; x < 288; x++)
            {
                fb[y * 320 + x] = color;
            }
        }

        s_frame_count++;
        MARS_COMM7_REG = s_frame_count; /* Report frame counter to Genesis */

        /* Flip framebuffer at VBlank */
        mars_vdp_flip();
    }
}
