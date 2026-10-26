/**
 * @file mars_master.c
 * @brief Sega 32X Master SH-2 Entry & Synchronization Test Orchestrator.
 *
 * Drives the multi-core IPC test harness and renders test pass/fail results
 * directly to the 32X Direct-Color (15bpp) framebuffer.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdbool.h>
#include "sh2_ipc.h"
#include "sh2_test.h"

/**
 * @brief Initializes 32X VDP in 320x224 Direct Color Mode (15bpp BGR).
 */
static void mars_vdp_init(void)
{
    MARS_VDP_MODE_REG = 0x0001;
}

/**
 * @brief Toggles active framebuffer and waits for VBlank swap acknowledgment.
 */
static void mars_vdp_flip(void)
{
    MARS_VDP_FS_REG ^= 0x0001;
    while ((MARS_VDP_FS_REG & 0x0002) != 0)
    {
        __asm__ volatile ("nop");
    }
}

/**
 * @brief Draws a solid rectangular block into the active framebuffer.
 */
static void draw_box(int x0, int y0, int w, int h, uint16_t color)
{
    volatile uint16_t *fb = MARS_FRAMEBUFFER;
    for (int y = y0; y < y0 + h; y++)
    {
        int row_offset = y * 320;
        for (int x = x0; x < x0 + w; x++)
        {
            fb[row_offset + x] = color;
        }
    }
}

/**
 * @brief Master SH-2 C entry point.
 */
void mars_master_main(void)
{
    /* Resolve uncached shared test state pointer */
    g_test_state = sh2_test_get_state();

    /* Initialize VDP display */
    mars_vdp_init();

    /* Zero and configure shared test state in uncached SDRAM */
    sh2_test_init_state(g_test_state);

    /* Execute the 5-part dual-core test suite */
    sh2_test_master_run(g_test_state);

    /* Colors in 15-bit BGR format */
    const uint16_t COLOR_BG    = 0x1084; /* Dark slate */
    const uint16_t COLOR_GREEN = 0x03E0; /* Pass */
    const uint16_t COLOR_RED   = 0x001F; /* Fail */
    const uint16_t COLOR_WHITE = 0x7FFF;

    uint32_t frame = 0;

    while (1)
    {
        frame++;

        /* Clear background */
        draw_box(0, 0, 320, 224, COLOR_BG);

        /* Draw title banner bar */
        draw_box(20, 16, 280, 8, COLOR_WHITE);

        /* Render status bars for each of the 5 tests */
        uint32_t passed = g_test_state->tests_passed;

        /* Test 1: Spinlock Mutex */
        uint16_t col1 = (passed & TEST_MASK_SPINLOCK) ? COLOR_GREEN : COLOR_RED;
        draw_box(40, 40, 240, 16, col1);

        /* Test 2: Barrier Sync */
        uint16_t col2 = (passed & TEST_MASK_BARRIER) ? COLOR_GREEN : COLOR_RED;
        draw_box(40, 68, 240, 16, col2);

        /* Test 3: COMM Semaphore */
        uint16_t col3 = (passed & TEST_MASK_COMM) ? COLOR_GREEN : COLOR_RED;
        draw_box(40, 96, 240, 16, col3);

        /* Test 4: Ring-Buffer FIFO Queue */
        uint16_t col4 = (passed & TEST_MASK_FIFO) ? COLOR_GREEN : COLOR_RED;
        draw_box(40, 124, 240, 16, col4);

        /* Test 5: PWM Audio FIFO */
        uint16_t col5 = (passed & TEST_MASK_PWM) ? COLOR_GREEN : COLOR_RED;
        draw_box(40, 152, 240, 16, col5);

        /* Pulsing heartbeat box */
        uint16_t pulse_col = (uint16_t)((frame & 0x1F) | (((frame >> 1) & 0x1F) << 5));
        draw_box(144, 180, 32, 16, pulse_col);

        /* Telemetry update to Genesis Main 68000 */
        MARS_COMM6_REG = (uint16_t)passed;
        MARS_COMM7_REG = (uint16_t)(frame & 0xFFFF);

        mars_vdp_flip();
    }
}
