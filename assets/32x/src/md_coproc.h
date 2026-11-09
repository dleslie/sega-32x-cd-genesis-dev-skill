// SPDX-License-Identifier: MIT
/**
 * @file md_coproc.h
 * @brief Sega 32X Motorola 68000 Coprocessor Offload Interface.
 *
 * Defines the dual-port communication protocol and data structures for offloading
 * ambient town NPC simulation and dungeon monster pathfinding step calculations
 * from the Master SH-2 to the Motorola 68000 host CPU via Mars COMM registers
 * and 68000 Work RAM.
 */

#ifndef D32_MD_COPROC_H
#define D32_MD_COPROC_H

#include <stdint.h>
#include <stddef.h>

/* Mars COMM0 Command Words for 68000 Coprocessor Offload */
#define D32_MD_CMD_TOWN_TICK        0x4100  /* Execute town NPC idle animation and step tick */
#define D32_MD_CMD_MONSTER_STEP     0x4200  /* Compute 8-way directional step & collision */

/* Fixed memory locations in Genesis 68000 Work RAM ($00FF0000..$00FFFFFF) */
#define D32_MD_RAM_COPROC_BASE      0x00FF1000

/* Total number of Tristram towners tracked by the ambient simulator */
#define D32_MD_COPROC_TOWNER_COUNT  15

/**
 * @brief Serialized towner state layout in 68000 Work RAM ($00FF1000).
 * Total size: 15 * 4 = 60 bytes.
 */
typedef struct {
    uint8_t active;      /* 1 if towner is currently spawned in town */
    uint8_t anim_frame;  /* Current animation cel index (0..anim_total-1) */
    uint8_t anim_cnt;    /* Current frame delay accumulator ticks */
    uint8_t anim_delay;  /* Tick threshold to advance frame (2 or 3) */
} D32MdTownerState;

/**
 * @brief Serialized monster step request/response layout in 68000 Work RAM ($00FF1040).
 * Total size: 16 bytes.
 */
typedef struct {
    int32_t target_x;    /* Target actor X (subunits) */
    int32_t target_y;    /* Target actor Y (subunits) */
    int32_t monster_x;   /* Moving monster X (subunits) */
    int32_t monster_y;   /* Moving monster Y (subunits) */
    int16_t speed;       /* Monster movement speed (subunits/tick) */
    uint8_t facing;      /* Computed facing direction (1=+x, 2=-x, 4=+y, 8=-y) */
    uint8_t walk_ticks;  /* Walk duration ticks (set to 4 when moved) */
    int32_t result_x;    /* Computed destination X (subunits) */
    int32_t result_y;    /* Computed destination Y (subunits) */
} D32MdMonsterStepJob;

/**
 * @brief Evaluates an 8-way monster step vector towards a target position.
 *
 * Symmetrically implemented in C for host tests/oracle and M68K assembly for 32X hardware.
 *
 * @param job Pointer to monster step job structure containing inputs and receiving results.
 */
void d32_md_coproc_calc_monster_step(D32MdMonsterStepJob *job);

/**
 * @brief Advances towner idle animations across all 15 Tristram towners.
 *
 * Symmetrically implemented in C for host tests/oracle and M68K assembly for 32X hardware.
 *
 * @param states Array of 15 towner animation states.
 * @param anim_totals Array of maximum frame counts per towner.
 */
void d32_md_coproc_tick_towners(D32MdTownerState *states, const uint8_t *anim_totals);

#endif /* D32_MD_COPROC_H */
