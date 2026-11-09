// SPDX-License-Identifier: MIT
/**
 * @file md_coproc.c
 * @brief Host reference and SH-2 dispatch implementation for 68000 Coprocessor Offload.
 */

#include "md_coproc.h"
#include "d32.h"
#include <string.h>

/**
 * @brief Computes 8-way directional step vector and facing direction.
 *
 * Implements the canonical movement logic matching step_toward() in actor.c.
 *
 * @param job Pointer to monster step job structure.
 */
void d32_md_coproc_calc_monster_step(D32MdMonsterStepJob *job)
{
    /* Invariant: validate pointer before executing */
    if (!job) {
        return;
    }

    int32_t dx = job->target_x - job->monster_x;
    int32_t dy = job->target_y - job->monster_y;
    int mx = 0, my = 0;

    /* Evaluate horizontal displacement direction */
    if (dx > 0) {
        mx = 1;
    } else if (dx < 0) {
        mx = -1;
    }

    /* Evaluate vertical displacement direction */
    if (dy > 0) {
        my = 1;
    } else if (dy < 0) {
        my = -1;
    }

    /* Pack 8-way facing direction: bit 0=+x, bit 1=-x, bit 2=+y, bit 3=-y */
    job->facing = (uint8_t)((mx > 0 ? 1 : mx < 0 ? 2 : 0) | (my > 0 ? 4 : my < 0 ? 8 : 0));

    /* Initialize result coordinates to current position */
    job->result_x = job->monster_x;
    job->result_y = job->monster_y;
    job->walk_ticks = 0;

    /* Advance monster coordinates when non-zero movement vector exists */
    if (mx || my) {
        int32_t step = ((int32_t)job->speed * D32_WALK_SPEED) >> 6;
        /* Ensure even the slowest monsters move by at least 1 sub-pixel unit per tick */
        if (step < 1) {
            step = 1;
        }

        job->walk_ticks = 4;
        if (mx) {
            job->result_x += (mx > 0 ? step : -step);
        }
        if (my) {
            job->result_y += (my > 0 ? step : -step);
        }
    }
}

/**
 * @brief Advances towner idle animations across all 15 Tristram towners.
 *
 * Matches the logic in d32_town_tick() in town.c.
 *
 * @param states Array of 15 towner animation states.
 * @param anim_totals Array of maximum frame counts per towner.
 */
void d32_md_coproc_tick_towners(D32MdTownerState *states, const uint8_t *anim_totals)
{
    /* Invariant: validate state arrays */
    if (!states || !anim_totals) {
        return;
    }

    for (int i = 0; i < D32_MD_COPROC_TOWNER_COUNT; i++) {
        D32MdTownerState *tw = &states[i];
        /* Skip inactive expansion towners */
        if (!tw->active) {
            continue;
        }

        tw->anim_cnt++;
        /* Delay threshold reached; advance to next animation frame */
        if (tw->anim_cnt >= tw->anim_delay) {
            tw->anim_cnt = 0;
            tw->anim_frame++;
            /* Loop animation back to frame 0 upon reaching total frames */
            if (tw->anim_frame >= anim_totals[i]) {
                tw->anim_frame = 0;
            }
        }
    }
}
