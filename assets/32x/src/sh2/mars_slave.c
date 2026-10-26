/**
 * @file mars_slave.c
 * @brief Sega 32X Slave SH-2 Worker & Audio Processing Loop.
 *
 * Participates in concurrent spinlock, barrier, and IPC message queue tests,
 * and maintains continuous PWM audio synthesis and heartbeat reporting.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdbool.h>
#include "sh2_ipc.h"
#include "sh2_test.h"

static uint32_t s_slave_ticks = 0;

/**
 * @brief Slave SH-2 C entry point.
 */
void mars_slave_main(void)
{
    /* Resolve uncached shared test state pointer */
    g_test_state = sh2_test_get_state();

    /* Run Slave side of dual-core synchronization and IPC test suite */
    sh2_test_slave_run(g_test_state);

    /* Continuous worker loop */
    while (1)
    {
        s_slave_ticks++;

        /* Feed simple test synthesizer wave into PWM FIFO if space is available */
        uint16_t sample = (uint16_t)((s_slave_ticks >> 3) & 0x01FF);
        sh2_pwm_fifo_write(sample);

        /* Report tick heartbeat back to Genesis over COMM3 */
        MARS_COMM3_REG = (uint16_t)(s_slave_ticks & 0xFFFF);
    }
}
