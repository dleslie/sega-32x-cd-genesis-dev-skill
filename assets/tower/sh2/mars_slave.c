/**
 * @file mars_slave.c
 * @brief Sega 32X Slave SH-2 Worker & Audio Processing Loop.
 *
 * Runs concurrent math coprocessing, rasterization workloads, and
 * feeds the 32X Stereo 12-bit PWM audio FIFO.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>

/* 32X PWM Registers */
#define MARS_PWM_CTRL_REG    (*(volatile uint16_t *)0x20004030)
#define MARS_PWM_CYCLE_REG   (*(volatile uint16_t *)0x20004032)
#define MARS_PWM_LCH_REG     (*(volatile uint16_t *)0x20004034)
#define MARS_PWM_RCH_REG     (*(volatile uint16_t *)0x20004036)
#define MARS_PWM_MONO_REG    (*(volatile uint16_t *)0x20004038)

/* Slave Status Reporting */
#define MARS_COMM3_REG       (*(volatile uint16_t *)0x20004026)

static uint32_t s_slave_ticks = 0;

void mars_pwm_init(void)
{
    /* Set sample cycle (e.g. 1045 for 22050 Hz playback) */
    MARS_PWM_CYCLE_REG = 1045;
    /* Enable PWM audio playback */
    MARS_PWM_CTRL_REG = 0x0185;
}

void mars_slave_main(void)
{
    mars_pwm_init();

    while (1)
    {
        /* Perform worker math calculations or audio sample generation */
        s_slave_ticks++;

        /* Simple synthesizer wave generation into PWM FIFO */
        uint16_t sample = (uint16_t)((s_slave_ticks >> 3) & 0x01FF);
        MARS_PWM_MONO_REG = sample;

        /* Report tick heartbeat back to Genesis */
        MARS_COMM3_REG = (uint16_t)(s_slave_ticks & 0xFFFF);
    }
}
