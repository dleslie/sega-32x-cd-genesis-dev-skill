/**
 * @file sh2_ipc.c
 * @brief Sega 32X Dual SH-2 IPC, Barrier, and Ring-Buffer Implementation.
 *
 * Implements phase barriers and spinlock-protected FIFO queues located in
 * the cache-through SDRAM aperture.
 *
 * SPDX-License-Identifier: MIT
 */

#include "sh2_ipc.h"

/**
 * @brief Waits at a phase barrier until all participating cores arrive.
 *
 * Uses the inner spinlock to serialize arrival increments. When the final
 * core reaches the barrier, it clears the arrival count and increments the
 * phase counter, releasing waiting cores.
 *
 * @param b Barrier located in cache-through uncached SDRAM space.
 * @param total_cores Required count of participating cores.
 */
void sh2_barrier_wait(volatile Sh2Barrier *b, uint32_t total_cores)
{
    /* Capture core ID from active stack pointer or caller */
    uint32_t target_phase;

    sh2_spinlock_lock(&b->lock, SH2_CORE_MASTER);

    target_phase = b->phase + 1;
    b->count++;

    if (b->count >= total_cores)
    {
        /* All cores arrived: advance phase and reset arrival counter */
        b->count = 0;
        b->phase = target_phase;
        sh2_spinlock_unlock(&b->lock);
        return;
    }

    /* Wait for the last core to advance the phase */
    sh2_spinlock_unlock(&b->lock);

    while (b->phase != target_phase)
    {
        __asm__ volatile ("nop\n\tnop");
    }
}

/**
 * @brief Initializes an inter-core FIFO message queue.
 * @param q Pointer to queue located in cache-through space.
 */
void sh2_queue_init(volatile Sh2Queue *q)
{
    sh2_spinlock_init(&q->lock);
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    q->capacity = SH2_QUEUE_CAPACITY;

    for (uint16_t i = 0; i < SH2_QUEUE_CAPACITY; i++)
    {
        q->packets[i].cmd = SH2_CMD_NONE;
        q->packets[i].seq = 0;
        q->packets[i].arg0 = 0;
        q->packets[i].arg1 = 0;
        q->packets[i].arg2 = 0;
        q->packets[i].result = 0;
        q->packets[i].status = SH2_STATUS_IDLE;
        q->packets[i].checksum = 0;
    }
}

/**
 * @brief Checks if queue is full without acquiring lock.
 */
bool sh2_queue_is_full(volatile Sh2Queue *q)
{
    return q->count >= q->capacity;
}

/**
 * @brief Checks if queue is empty without acquiring lock.
 */
bool sh2_queue_is_empty(volatile Sh2Queue *q)
{
    return q->count == 0;
}

/**
 * @brief Enqueues a packet into the FIFO queue under spinlock protection.
 * @param q Target queue.
 * @param pkt Packet to copy.
 * @param core_id Requesting core ID.
 * @return True if enqueued successfully; false if queue is full.
 */
bool sh2_queue_push(volatile Sh2Queue *q, const Sh2Packet *pkt, uint8_t core_id)
{
    sh2_spinlock_lock(&q->lock, core_id);

    if (q->count >= q->capacity)
    {
        sh2_spinlock_unlock(&q->lock);
        return false;
    }

    uint16_t idx = q->tail;
    q->packets[idx] = *pkt;

    /* Advance ring buffer tail with wrapping */
    uint16_t next_tail = idx + 1;
    q->tail = (next_tail >= q->capacity) ? 0 : next_tail;
    q->count++;

    sh2_spinlock_unlock(&q->lock);
    return true;
}

/**
 * @brief Dequeues a packet from the FIFO queue under spinlock protection.
 * @param q Target queue.
 * @param pkt Destination packet buffer.
 * @param core_id Requesting core ID.
 * @return True if dequeued successfully; false if queue was empty.
 */
bool sh2_queue_pop(volatile Sh2Queue *q, Sh2Packet *pkt, uint8_t core_id)
{
    sh2_spinlock_lock(&q->lock, core_id);

    if (q->count == 0)
    {
        sh2_spinlock_unlock(&q->lock);
        return false;
    }

    uint16_t idx = q->head;
    *pkt = q->packets[idx];

    /* Mark slot idle and advance head */
    q->packets[idx].status = SH2_STATUS_IDLE;
    uint16_t next_head = idx + 1;
    q->head = (next_head >= q->capacity) ? 0 : next_head;
    q->count--;

    sh2_spinlock_unlock(&q->lock);
    return true;
}
