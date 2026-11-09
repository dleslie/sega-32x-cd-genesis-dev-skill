/**
 * @file sh2_test.h
 * @brief Sega 32X Dual SH-2 Multiprocessing & Synchronization Test Suite.
 *
 * Verifies:
 *   1. Hardware TAS.B spinlock mutual exclusion under contention
 *   2. Dual-core barrier phase synchronization
 *   3. Hardware COMM register sequence-stamped semaphore ping-pong
 *   4. Inter-core shared FIFO ring-buffer message queues
 *   5. Hardware PWM audio FIFO burst and status flag handling
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _SH2_TEST_H_
#define _SH2_TEST_H_

#include "sh2_ipc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Test Bitmask Flags */
#define TEST_MASK_SPINLOCK   (1 << 0)
#define TEST_MASK_BARRIER    (1 << 1)
#define TEST_MASK_COMM       (1 << 2)
#define TEST_MASK_FIFO       (1 << 3)
#define TEST_MASK_PWM        (1 << 4)
#define TEST_MASK_ALL        (0x1F)

#define SPINLOCK_TEST_ITERS  200

/**
 * @struct Sh2SharedTestState
 * @brief Memory layout residing in uncached SDRAM (0x26000000 aperture).
 */
typedef struct {
    /* Test 1: Spinlock mutual exclusion */
    Sh2Spinlock mutex;
    volatile uint32_t shared_counter;
    volatile uint32_t spinlock_contention_cycles;

    /* Test 2: Barrier synchronization */
    Sh2Barrier barrier;
    volatile uint32_t barrier_step_master;
    volatile uint32_t barrier_step_slave;

    /* Test 3: COMM register semaphore */
    volatile uint32_t comm_pingpong_ack;

    /* Test 4: Bidirectional FIFO queues */
    Sh2Queue master_to_slave_q;
    Sh2Queue slave_to_master_q;

    /* Global Test Status & Telemetry */
    volatile uint32_t tests_passed;
    volatile uint32_t tests_failed;
    volatile uint32_t test_mask;
    volatile uint32_t active_test;
    volatile bool master_ready;
    volatile bool master_done;
    volatile bool slave_done;
} Sh2SharedTestState;

/**
 * @brief Storage allocated in SDRAM and accessor for uncached aperture.
 */
extern Sh2SharedTestState s_shared_state_storage;
extern volatile Sh2SharedTestState *g_test_state;

static inline volatile Sh2SharedTestState *sh2_test_get_state(void)
{
    return (volatile Sh2SharedTestState *)SH2_UNCACHED(&s_shared_state_storage);
}

void sh2_test_init_state(volatile Sh2SharedTestState *state);

/* Master SH-2 Test Steps */
void sh2_test_master_run(volatile Sh2SharedTestState *state);

/* Slave SH-2 Test Steps */
void sh2_test_slave_run(volatile Sh2SharedTestState *state);

#ifdef __cplusplus
}
#endif

#endif /* _SH2_TEST_H_ */
