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

        /* 2. Concurrently render bottom screen half (y in [100, 200)) in back-to-front order */
        uint8_t q_count = shared->queue_count;
        for (int i = 0; i < q_count; i++)
        {
            uint8_t item_type = shared->sorted_queue[i].item_type;
            uint16_t color    = shared->sorted_queue[i].color;

            if (item_type == ITEM_TYPE_TRIANGLE)
            {
                uint8_t tri_idx = shared->sorted_queue[i].tri_idx;
                uint8_t v0 = s_ship_triangles[tri_idx].v0;
                uint8_t v1 = s_ship_triangles[tri_idx].v1;
                uint8_t v2 = s_ship_triangles[tri_idx].v2;

                draw_flat_triangle_clipped(&shared->projected[v0],
                                           &shared->projected[v1],
                                           &shared->projected[v2],
                                           color, FB_HEIGHT / 2, FB_HEIGHT);
            }
            else if (item_type == ITEM_TYPE_FLAME)
            {
                int32_t ex = shared->projected[15].sx;
                int32_t ey = shared->projected[15].sy;
                draw_flame_light(fb, ex, ey, color, FB_HEIGHT / 2, FB_HEIGHT);
            }
            else if (item_type == ITEM_TYPE_STROBE_L)
            {
                int32_t lx = shared->projected[9].sx;
                int32_t ly = shared->projected[9].sy;
                draw_strobe_pixel(fb, lx, ly, color, FB_HEIGHT / 2, FB_HEIGHT);
            }
            else if (item_type == ITEM_TYPE_STROBE_R)
            {
                int32_t rx = shared->projected[10].sx;
                int32_t ry = shared->projected[10].sy;
                draw_strobe_pixel(fb, rx, ry, color, FB_HEIGHT / 2, FB_HEIGHT);
            }
        }

        /* 3. Acknowledge completion to Master SH-2 */
        s_slave_work_count++;
        shared->slave_ack = s_slave_work_count;
        MARS_COMM3_REG = current_frame;
    }
}
