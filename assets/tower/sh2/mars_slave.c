/**
 * @file mars_slave.c
 * @brief Sega 32X Slave SH-2 Worker & Concurrent 3D Rasterizer.
 *
 * Runs concurrent 3D flat-shaded polygon rasterization (Batch 2: Wings, Stabilizers,
 * Keel, and Engines) in parallel with Master SH-2. Silences PWM audio squeal.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdbool.h>
#include "sh2_ipc.h"
#include "sh2_test.h"
#include "starfighter.h"
#include "star_raster.h"

extern StarSharedState g_shared_state;

static uint16_t s_last_frame = 0xFFFF;
static uint16_t s_slave_work_count = 0;

/**
 * @brief Silences 32X PWM audio registers to eliminate high-pitched audio squeal.
 */
void mars_pwm_init(void)
{
    /* Completely disable PWM audio generator to ensure silence */
    MARS_PWM_CTRL_REG = 0x0000;
}

/**
 * @brief Slave SH-2 main worker routine.
 */
void mars_slave_main(void)
{
    /* Initialize uncached test state and run slave test sequence */
    g_test_state = sh2_test_get_state();
    sh2_test_slave_run(g_test_state);

    mars_pwm_init();

    StarSharedState *shared = (StarSharedState *)SH2_UNCACHED(&g_shared_state);
    volatile uint16_t *fb = (volatile uint16_t *)0x24000000;

    while (1)
    {
        /* 1. Poll for frame dispatch signal from Master SH-2 */
        uint16_t current_frame = MARS_COMM2_REG;
        if (current_frame == s_last_frame)
        {
            __asm__ volatile ("nop");
            continue;
        }
        s_last_frame = current_frame;

        /* 2. Concurrently Rasterize Batch 2 (Wings, Stabilizers, Keel, Engines) */
        for (int i = 0; i < 11; i++)
        {
            render_triangle_flat(&s_batch2_triangles[i], shared);
        }

        /* 3. Render wingtip navigation strobe beacons */
        int32_t lx = shared->projected[9].sx,  ly = shared->projected[9].sy;
        int32_t rx = shared->projected[10].sx, ry = shared->projected[10].sy;

        /* Port wingtip: Emerald green navigation strobe */
        if (lx >= 0 && lx < FB_WIDTH && ly >= 0 && ly < FB_HEIGHT)
        {
            uint16_t port_col = ((s_last_frame & 8) == 0) ? (0x8000 | (4 << 10) | (31 << 5) | 4) :
                                                            (0x8000 | (2 << 10) | (14 << 5) | 2);
            fb[256 + (ly * 320) + lx] = port_col;
        }

        /* Starboard wingtip: Crimson red navigation strobe */
        if (rx >= 0 && rx < FB_WIDTH && ry >= 0 && ry < FB_HEIGHT)
        {
            uint16_t stbd_col = ((s_last_frame & 8) == 0) ? (0x8000 | (4 << 10) | (4 << 5) | 31) :
                                                            (0x8000 | (2 << 10) | (2 << 5) | 14);
            fb[256 + (ry * 320) + rx] = stbd_col;
        }

        /* 4. Acknowledge completion of Batch 2 to Master SH-2 */
        s_slave_work_count++;
        shared->slave_ack = s_slave_work_count;
        MARS_COMM3_REG = current_frame;
    }
}
