/**
 * @file sh2_test.c
 * @brief Sega 32X Dual SH-2 Multiprocessing Test Suite Implementation.
 *
 * Implements hardware verification tests across both SH-2 cores.
 *
 * SPDX-License-Identifier: MIT
 */

#include "sh2_test.h"

/* Allocation in SDRAM data/bss section */
Sh2SharedTestState s_shared_state_storage;
volatile Sh2SharedTestState *g_test_state = 0;

/**
 * @brief Zeroes and initializes shared test state in uncached SDRAM.
 */
void sh2_test_init_state(volatile Sh2SharedTestState *state)
{
    sh2_spinlock_init(&state->mutex);
    state->shared_counter = 0;
    state->spinlock_contention_cycles = 0;

    sh2_barrier_init(&state->barrier);
    state->barrier_step_master = 0;
    state->barrier_step_slave = 0;

    state->comm_pingpong_ack = 0;

    sh2_queue_init(&state->master_to_slave_q);
    sh2_queue_init(&state->slave_to_master_q);

    state->tests_passed = 0;
    state->tests_failed = 0;
    state->test_mask = 0;
    state->active_test = 0;
    state->master_ready = true;
    state->master_done = false;
    state->slave_done = false;
}

/**
 * @brief Master SH-2 test runner.
 */
void sh2_test_master_run(volatile Sh2SharedTestState *state)
{
    /* ---------------------------------------------------------------------- */
    /* Test 1: Spinlock Mutual Exclusion Under High Contention                */
    /* ---------------------------------------------------------------------- */
    state->active_test = 1;
    sh2_barrier_wait(&state->barrier, 2);

    for (uint32_t i = 0; i < SPINLOCK_TEST_ITERS; i++)
    {
        sh2_spinlock_lock(&state->mutex, SH2_CORE_MASTER);
        /* Critical section: increment shared counter */
        state->shared_counter++;
        sh2_spinlock_unlock(&state->mutex);
    }

    /* Wait for Slave core to finish its iterations */
    sh2_barrier_wait(&state->barrier, 2);

    /* Total counter must equal exactly 2 * SPINLOCK_TEST_ITERS */
    uint32_t expected_total = SPINLOCK_TEST_ITERS * 2;
    if (state->shared_counter == expected_total)
    {
        state->tests_passed |= TEST_MASK_SPINLOCK;
    }
    else
    {
        state->tests_failed |= TEST_MASK_SPINLOCK;
    }

    /* ---------------------------------------------------------------------- */
    /* Test 2: Barrier Phase Synchronization                                 */
    /* ---------------------------------------------------------------------- */
    state->active_test = 2;
    bool barrier_ok = true;

    for (uint32_t phase = 1; phase <= 4; phase++)
    {
        state->barrier_step_master = phase;
        sh2_barrier_wait(&state->barrier, 2);

        /* Confirm both cores reached the same phase */
        if (state->barrier_step_slave != phase)
        {
            barrier_ok = false;
        }

        sh2_barrier_wait(&state->barrier, 2);
    }

    if (barrier_ok)
    {
        state->tests_passed |= TEST_MASK_BARRIER;
    }
    else
    {
        state->tests_failed |= TEST_MASK_BARRIER;
    }

    /* ---------------------------------------------------------------------- */
    /* Test 3: Hardware COMM Register Semaphore Ping-Pong                    */
    /* ---------------------------------------------------------------------- */
    state->active_test = 3;
    bool comm_ok = true;

    for (uint8_t seq = 1; seq <= 8; seq++)
    {
        uint8_t payload = (uint8_t)(seq * 11);
        sh2_comm_send(&MARS_COMM4_REG, seq, payload);

        /* Wait for Slave ACK on COMM5 */
        uint32_t timeout = 50000;
        uint8_t resp_seq = 0;
        uint8_t resp_payload = 0;
        bool got_ack = false;

        while (--timeout)
        {
            if (sh2_comm_poll(&MARS_COMM5_REG, &resp_seq, &resp_payload))
            {
                if (resp_seq == seq && resp_payload == (uint8_t)(payload ^ 0x5A))
                {
                    got_ack = true;
                    break;
                }
            }
        }

        if (!got_ack)
        {
            comm_ok = false;
            break;
        }
    }

    if (comm_ok)
    {
        state->tests_passed |= TEST_MASK_COMM;
    }
    else
    {
        state->tests_failed |= TEST_MASK_COMM;
    }

    /* ---------------------------------------------------------------------- */
    /* Test 4: Ring-Buffer Shared FIFO Message Queues                         */
    /* ---------------------------------------------------------------------- */
    state->active_test = 4;
    bool fifo_ok = true;

    /* Enqueue 8 3D vector calculation requests to Slave */
    for (uint16_t seq = 1; seq <= 8; seq++)
    {
        Sh2Packet pkt;
        pkt.cmd = SH2_CMD_VEC_TRANSFORM;
        pkt.seq = seq;
        pkt.arg0 = (int32_t)(seq * 10);  /* X */
        pkt.arg1 = (int32_t)(seq * 20);  /* Y */
        pkt.arg2 = (int32_t)(seq * 30);  /* Z */
        pkt.result = 0;
        pkt.status = SH2_STATUS_QUEUED;
        pkt.checksum = sh2_packet_calc_checksum(&pkt);

        while (!sh2_queue_push(&state->master_to_slave_q, &pkt, SH2_CORE_MASTER))
        {
            __asm__ volatile ("nop");
        }
    }

    /* Receive 8 processed response packets from Slave */
    for (uint16_t expected_seq = 1; expected_seq <= 8; expected_seq++)
    {
        Sh2Packet resp;
        uint32_t timeout = 50000;
        bool popped = false;

        while (--timeout)
        {
            if (sh2_queue_pop(&state->slave_to_master_q, &resp, SH2_CORE_MASTER))
            {
                popped = true;
                break;
            }
        }

        if (!popped)
        {
            fifo_ok = false;
            break;
        }

        /* Verify packet sequence, status, and checksum */
        uint16_t expected_chk = sh2_packet_calc_checksum(&resp);
        int32_t expected_res = (resp.arg0 * resp.arg0 + resp.arg1 * resp.arg1 + resp.arg2 * resp.arg2);

        if (resp.seq != expected_seq ||
            resp.status != SH2_STATUS_OK ||
            resp.checksum != expected_chk ||
            resp.result != expected_res)
        {
            fifo_ok = false;
            break;
        }
    }

    if (fifo_ok)
    {
        state->tests_passed |= TEST_MASK_FIFO;
    }
    else
    {
        state->tests_failed |= TEST_MASK_FIFO;
    }

    /* ---------------------------------------------------------------------- */
    /* Test 5: Hardware PWM Audio FIFO Status & Burst                        */
    /* ---------------------------------------------------------------------- */
    state->active_test = 5;
    bool pwm_ok = true;

    /* Initialize PWM cycle rate to 22050 Hz (1045 cycles) */
    MARS_PWM_CYCLE_REG = 1045;
    MARS_PWM_CTRL_REG = 0x0185;

    /* Write 3 samples to fill the 3-entry 32X PWM FIFO */
    for (int i = 0; i < 3; i++)
    {
        if (!sh2_pwm_fifo_write((uint16_t)(1043 + i * 10)))
        {
            pwm_ok = false;
            break;
        }
    }

    /* Verify that the FIFO correctly signals full on the next push */
    if (sh2_pwm_fifo_write(1043))
    {
        pwm_ok = false;
    }

    if (pwm_ok)
    {
        state->tests_passed |= TEST_MASK_PWM;
    }
    else
    {
        state->tests_failed |= TEST_MASK_PWM;
    }

    /* ---------------------------------------------------------------------- */
    /* Final Telemetry & Genesis Notification                                 */
    /* ---------------------------------------------------------------------- */
    state->active_test = 0;
    state->master_done = true;

    /* Report test completion and passed mask to Genesis over COMM6/COMM7 */
    MARS_COMM6_REG = (uint16_t)state->tests_passed;
    MARS_COMM7_REG = (uint16_t)state->tests_failed;
}

/**
 * @brief Slave SH-2 test runner.
 */
void sh2_test_slave_run(volatile Sh2SharedTestState *state)
{
    /* Wait until Master has initialized shared SDRAM test structures */
    while (!state->master_ready)
    {
        __asm__ volatile ("nop");
    }

    /* ---------------------------------------------------------------------- */
    /* Test 1: Spinlock Mutual Exclusion Under High Contention                */
    /* ---------------------------------------------------------------------- */
    sh2_barrier_wait(&state->barrier, 2);

    for (uint32_t i = 0; i < SPINLOCK_TEST_ITERS; i++)
    {
        sh2_spinlock_lock(&state->mutex, SH2_CORE_SLAVE);
        /* Critical section: increment shared counter */
        state->shared_counter++;
        sh2_spinlock_unlock(&state->mutex);
    }

    sh2_barrier_wait(&state->barrier, 2);

    /* ---------------------------------------------------------------------- */
    /* Test 2: Barrier Phase Synchronization                                 */
    /* ---------------------------------------------------------------------- */
    for (uint32_t phase = 1; phase <= 4; phase++)
    {
        state->barrier_step_slave = phase;
        sh2_barrier_wait(&state->barrier, 2);
        sh2_barrier_wait(&state->barrier, 2);
    }

    /* ---------------------------------------------------------------------- */
    /* Test 3: Hardware COMM Register Semaphore Ping-Pong                    */
    /* ---------------------------------------------------------------------- */
    uint8_t last_seq = 0;
    for (int i = 0; i < 8; i++)
    {
        uint8_t seq = 0;
        uint8_t payload = 0;

        /* Poll COMM4 for Master signal */
        while (!sh2_comm_poll(&MARS_COMM4_REG, &last_seq, &payload))
        {
            __asm__ volatile ("nop");
        }

        seq = last_seq;
        /* Respond with sequence and inverted mask on COMM5 */
        uint8_t reply_payload = (uint8_t)(payload ^ 0x5A);
        sh2_comm_send(&MARS_COMM5_REG, seq, reply_payload);
    }

    /* ---------------------------------------------------------------------- */
    /* Test 4: Ring-Buffer Shared FIFO Message Queues                         */
    /* ---------------------------------------------------------------------- */
    for (int i = 0; i < 8; i++)
    {
        Sh2Packet pkt;

        /* Wait for packet from Master */
        while (!sh2_queue_pop(&state->master_to_slave_q, &pkt, SH2_CORE_SLAVE))
        {
            __asm__ volatile ("nop");
        }

        /* Process packet: compute 3D vector length squared (x*x + y*y + z*z) */
        pkt.result = (pkt.arg0 * pkt.arg0 + pkt.arg1 * pkt.arg1 + pkt.arg2 * pkt.arg2);
        pkt.status = SH2_STATUS_OK;
        pkt.checksum = sh2_packet_calc_checksum(&pkt);

        /* Return response to Master */
        while (!sh2_queue_push(&state->slave_to_master_q, &pkt, SH2_CORE_SLAVE))
        {
            __asm__ volatile ("nop");
        }
    }

    /* ---------------------------------------------------------------------- */
    /* Test 5: Audio / Workload Completion                                    */
    /* ---------------------------------------------------------------------- */
    state->slave_done = true;
}
