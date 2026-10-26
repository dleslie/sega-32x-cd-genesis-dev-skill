/**
 * @file sh2_ipc.h
 * @brief Sega 32X Dual SH-2 Inter-Processor Communication (IPC) & Spinlock API.
 *
 * Provides cache-through uncached memory mapping, hardware bus-locking atomic
 * TAS.B spinlocks, barrier synchronization, hardware COMM register semaphores,
 * PWM audio FIFO controls, and a multi-packet inter-core message queue.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _SH2_IPC_H_
#define _SH2_IPC_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/* 1. Memory Aperture Translation Macros                                      */
/* ========================================================================== */

/**
 * @brief Translates an SDRAM address to the SH-2 cache-through aperture (Area 3).
 *
 * Dual SH-2s contain private 4 KB direct-mapped caches. Shared IPC data structures,
 * spinlocks, barriers, and FIFO queues must be accessed through the cache-through
 * aperture (bit 29 = 1, address 0x26000000) so that memory writes by one CPU are
 * immediately observable by the other CPU without cache coherence hazards.
 */
#define SH2_UNCACHED(addr) ((void *)((uintptr_t)(addr) | (uintptr_t)0x20000000))

/**
 * @brief Translates an address to the SH-2 cached aperture.
 */
#define SH2_CACHED(addr)   ((void *)((uintptr_t)(addr) & ~(uintptr_t)0x20000000))

/* ========================================================================== */
/* 2. 32X Hardware Registers (Cache-Through Aperture 0x20004000)               */
/* ========================================================================== */

/* 16-bit Communication Registers */
#define MARS_COMM0_REG       (*(volatile uint16_t *)0x20004020)
#define MARS_COMM1_REG       (*(volatile uint16_t *)0x20004022)
#define MARS_COMM2_REG       (*(volatile uint16_t *)0x20004024)
#define MARS_COMM3_REG       (*(volatile uint16_t *)0x20004026)
#define MARS_COMM4_REG       (*(volatile uint16_t *)0x20004028)
#define MARS_COMM5_REG       (*(volatile uint16_t *)0x2000402A)
#define MARS_COMM6_REG       (*(volatile uint16_t *)0x2000402C)
#define MARS_COMM7_REG       (*(volatile uint16_t *)0x2000402E)

/* 32-bit Communication Register Aliases */
#define MARS_COMM0_32        (*(volatile uint32_t *)0x20004020)
#define MARS_COMM2_32        (*(volatile uint32_t *)0x20004024)
#define MARS_COMM4_32        (*(volatile uint32_t *)0x20004028)
#define MARS_COMM6_32        (*(volatile uint32_t *)0x2000402C)

/* PWM Audio FIFO & Control Registers */
#define MARS_PWM_CTRL_REG    (*(volatile uint16_t *)0x20004030)
#define MARS_PWM_CYCLE_REG   (*(volatile uint16_t *)0x20004032)
#define MARS_PWM_LCH_REG     (*(volatile uint16_t *)0x20004034)
#define MARS_PWM_RCH_REG     (*(volatile uint16_t *)0x20004036)
#define MARS_PWM_MONO_REG    (*(volatile uint16_t *)0x20004038)

/* PWM Status Flags */
#define MARS_PWM_CTRL_FULL_L  ((uint16_t)0x8000) /**< Left Channel FIFO Full  */
#define MARS_PWM_CTRL_EMPTY_L ((uint16_t)0x4000) /**< Left Channel FIFO Empty */
#define MARS_PWM_CTRL_FULL_R  ((uint16_t)0x0080) /**< Right Channel FIFO Full */
#define MARS_PWM_CTRL_EMPTY_R ((uint16_t)0x0040) /**< Right Channel FIFO Empty*/

/* 32X VDP Framebuffer & Display Registers */
#define MARS_VDP_MODE_REG    (*(volatile uint16_t *)0x20004180)
#define MARS_VDP_FS_REG      (*(volatile uint16_t *)0x2000418A)
#define MARS_FRAMEBUFFER     ((volatile uint16_t *)0x24000000)

/* Core ID Identifiers */
#define SH2_CORE_MASTER      ((uint8_t)1)
#define SH2_CORE_SLAVE       ((uint8_t)2)

/* ========================================================================== */
/* 3. Atomic Spinlock Primitives (TAS.B)                                       */
/* ========================================================================== */

/**
 * @struct Sh2Spinlock
 * @brief Cache-coherent hardware atomic spinlock structure.
 */
typedef struct {
    volatile uint8_t lock;      /**< 0 = Unlocked, 0x80 = Locked (set by TAS.B) */
    volatile uint8_t owner;     /**< SH2_CORE_MASTER or SH2_CORE_SLAVE          */
    volatile uint16_t padding;  /**< Natural 4-byte boundary alignment          */
} Sh2Spinlock;

/**
 * @brief Initializes a spinlock structure to the unlocked state.
 * @param sl Pointer to the spinlock in uncached SDRAM space.
 */
static inline void sh2_spinlock_init(volatile Sh2Spinlock *sl)
{
    sl->lock = 0;
    sl->owner = 0;
    sl->padding = 0;
}

/**
 * @brief Executes a single atomic Test-and-Set byte (TAS.B) bus-cycle.
 * @param lock_byte Pointer to target lock byte in uncached memory.
 * @return True if the lock was free and was successfully acquired; false otherwise.
 */
static inline bool sh2_tas(volatile uint8_t *lock_byte)
{
    uint32_t result;
    /*
     * TAS.B reads the byte, sets the T-bit if the value was 0, and sets bit 7 (0x80).
     * The hardware asserts bus-lock on the 32X internal bus during read-modify-write.
     * MOVT transfers the T-bit into the output register.
     */
    __asm__ volatile (
        "tas.b  @%1\n\t"
        "movt   %0"
        : "=r" (result)
        : "r" (lock_byte)
        : "t", "memory"
    );
    return result != 0;
}

/**
 * @brief Attempts non-blocking acquisition of the spinlock.
 * @param sl Pointer to spinlock in uncached space.
 * @param core_id Core identifier (SH2_CORE_MASTER or SH2_CORE_SLAVE).
 * @return True on successful acquisition, false if already held.
 */
static inline bool sh2_spinlock_trylock(volatile Sh2Spinlock *sl, uint8_t core_id)
{
    if (sh2_tas(&sl->lock))
    {
        sl->owner = core_id;
        return true;
    }
    return false;
}

/**
 * @brief Acquires the spinlock, spinning until available.
 * @param sl Pointer to spinlock in uncached space.
 * @param core_id Core identifier.
 */
static inline void sh2_spinlock_lock(volatile Sh2Spinlock *sl, uint8_t core_id)
{
    while (!sh2_spinlock_trylock(sl, core_id))
    {
        /* Insert NOP pause to reduce SDRAM memory bus arbitration pressure */
        __asm__ volatile ("nop\n\tnop");
    }
}

/**
 * @brief Releases the spinlock and clears the owner.
 * @param sl Pointer to spinlock in uncached space.
 */
static inline void sh2_spinlock_unlock(volatile Sh2Spinlock *sl)
{
    /* Compiler memory barrier ensures all preceding writes are committed */
    __asm__ volatile ("" ::: "memory");
    sl->owner = 0;
    sl->lock = 0;
}

/* ========================================================================== */
/* 4. Barrier Synchronization                                                 */
/* ========================================================================== */

/**
 * @struct Sh2Barrier
 * @brief Dual-core phase barrier for coordinated multi-stage execution.
 */
typedef struct {
    Sh2Spinlock lock;           /**< Spinlock guarding count and phase   */
    volatile uint32_t phase;    /**< Incrementing phase sequence counter */
    volatile uint32_t count;    /**< Count of cores currently arrived    */
} Sh2Barrier;

/**
 * @brief Initializes a barrier structure.
 * @param b Barrier in uncached space.
 */
static inline void sh2_barrier_init(volatile Sh2Barrier *b)
{
    sh2_spinlock_init(&b->lock);
    b->phase = 0;
    b->count = 0;
}

/**
 * @brief Synchronizes both SH-2 cores across a barrier boundary.
 * @param b Barrier in uncached space.
 * @param total_cores Number of cores required to release barrier (normally 2).
 */
void sh2_barrier_wait(volatile Sh2Barrier *b, uint32_t total_cores);

/* ========================================================================== */
/* 5. Inter-Core FIFO Message Queue                                           */
/* ========================================================================== */

#define SH2_QUEUE_CAPACITY 16

/**
 * @enum Sh2CmdType
 * @brief IPC message command opcodes.
 */
typedef enum {
    SH2_CMD_NONE           = 0x0000,
    SH2_CMD_ECHO           = 0x0001,
    SH2_CMD_VEC_TRANSFORM  = 0x0002,
    SH2_CMD_AUDIO_SYNTH    = 0x0003,
    SH2_CMD_BENCHMARK      = 0x0004,
    SH2_CMD_TERMINATE      = 0x00FF
} Sh2CmdType;

/**
 * @enum Sh2StatusCode
 * @brief IPC message execution and response status codes.
 */
typedef enum {
    SH2_STATUS_IDLE        = 0x0000,
    SH2_STATUS_QUEUED      = 0x0001,
    SH2_STATUS_PROCESSING  = 0x0002,
    SH2_STATUS_OK          = 0x0003,
    SH2_STATUS_ERROR       = 0x0004
} Sh2StatusCode;

/**
 * @struct Sh2Packet
 * @brief Fixed-size IPC command / response packet.
 */
typedef struct {
    uint16_t cmd;        /**< Command opcode (Sh2CmdType)       */
    uint16_t seq;        /**< Sequence counter for replay guard */
    int32_t  arg0;       /**< 3D X coordinate or sound freq     */
    int32_t  arg1;       /**< 3D Y coordinate or sound pitch    */
    int32_t  arg2;       /**< 3D Z coordinate or sound volume   */
    int32_t  result;     /**< Arithmetic output from worker     */
    uint16_t status;     /**< Status code (Sh2StatusCode)       */
    uint16_t checksum;   /**< XOR verification checksum         */
} Sh2Packet;

/**
 * @struct Sh2Queue
 * @brief Circular FIFO message ring buffer protected by atomic spinlock.
 */
typedef struct {
    Sh2Spinlock lock;
    volatile uint16_t head;
    volatile uint16_t tail;
    volatile uint16_t count;
    volatile uint16_t capacity;
    Sh2Packet packets[SH2_QUEUE_CAPACITY];
} Sh2Queue;

/**
 * @brief Calculates integrity checksum for an IPC packet.
 */
static inline uint16_t sh2_packet_calc_checksum(const Sh2Packet *pkt)
{
    /* XOR all fields except the checksum field itself */
    uint32_t sum = (uint32_t)pkt->cmd ^ (uint32_t)pkt->seq ^
                   (uint32_t)(pkt->arg0 & 0xFFFF) ^ (uint32_t)((pkt->arg0 >> 16) & 0xFFFF) ^
                   (uint32_t)(pkt->arg1 & 0xFFFF) ^ (uint32_t)((pkt->arg1 >> 16) & 0xFFFF) ^
                   (uint32_t)(pkt->arg2 & 0xFFFF) ^ (uint32_t)((pkt->arg2 >> 16) & 0xFFFF) ^
                   (uint32_t)(pkt->result & 0xFFFF) ^ (uint32_t)((pkt->result >> 16) & 0xFFFF) ^
                   (uint32_t)pkt->status;
    return (uint16_t)(sum & 0xFFFF);
}

void sh2_queue_init(volatile Sh2Queue *q);
bool sh2_queue_push(volatile Sh2Queue *q, const Sh2Packet *pkt, uint8_t core_id);
bool sh2_queue_pop(volatile Sh2Queue *q, Sh2Packet *pkt, uint8_t core_id);
bool sh2_queue_is_full(volatile Sh2Queue *q);
bool sh2_queue_is_empty(volatile Sh2Queue *q);

/* ========================================================================== */
/* 6. Hardware COMM Register Semaphore Protocols                              */
/* ========================================================================== */

/**
 * @brief Sends a sequence-stamped word over a hardware COMM register.
 * @param reg_word Pointer to target COMM register (e.g. &MARS_COMM4_REG).
 * @param seq Sequence number to avoid race conditions.
 * @param payload 8-bit command payload.
 */
static inline void sh2_comm_send(volatile uint16_t *reg_word, uint8_t seq, uint8_t payload)
{
    *reg_word = (uint16_t)(((uint16_t)seq << 8) | payload);
}

/**
 * @brief Polls a hardware COMM register for a new sequence update.
 * @param reg_word Pointer to COMM register.
 * @param last_seq In/Out sequence counter. Updated when a new sequence arrives.
 * @param payload Output pointer for payload byte.
 * @return True if a new command arrived; false otherwise.
 */
static inline bool sh2_comm_poll(volatile uint16_t *reg_word, uint8_t *last_seq, uint8_t *payload)
{
    uint16_t val = *reg_word;
    uint8_t seq = (uint8_t)(val >> 8);
    if (seq != *last_seq && seq != 0)
    {
        *last_seq = seq;
        *payload = (uint8_t)(val & 0xFF);
        return true;
    }
    return false;
}

/* ========================================================================== */
/* 7. Hardware PWM Audio FIFO Messaging                                       */
/* ========================================================================== */

/**
 * @brief Writes a 12-bit mono sample to the 32X PWM hardware FIFO.
 * @param sample 12-bit PCM sample (0..4095, neutral midpoint = 1043).
 * @return True if pushed successfully; false if FIFO was full.
 */
static inline bool sh2_pwm_fifo_write(uint16_t sample)
{
    if ((MARS_PWM_CTRL_REG & (MARS_PWM_CTRL_FULL_L | MARS_PWM_CTRL_FULL_R)) == 0)
    {
        MARS_PWM_MONO_REG = sample;
        return true;
    }
    return false;
}

#ifdef __cplusplus
}
#endif

#endif /* _SH2_IPC_H_ */
