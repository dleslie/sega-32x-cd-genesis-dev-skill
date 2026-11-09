/**
 * @file profile.c
 * @brief Cycle-accurate CPU performance profiling and hardware memory telemetry implementation.
 */

#include "profile.h"
#include "font.h"
#include <stdio.h>
#include <string.h>

/* Global profiler telemetry snapshot instance */
D32ProfileTelemetry g_d32_profile;

/**
 * @brief Initializes performance profiler and memory instrumentation tracking.
 *
 * Configures fixed memory capacities, records BSS and stack boundaries,
 * and sets default 60 FPS baselines.
 *
 * @param prof Target profiler instance.
 * @param data_bss_bytes Total size of initialized data and BSS memory in bytes.
 * @param stack_base Base address of dynamically monitored stack arena.
 * @param stack_top Top of the stack address (initial SP value, e.g. 0x0603F000).
 */
void d32_profile_init(D32ProfileTelemetry *prof, uint32_t data_bss_bytes, uint32_t stack_base, uint32_t stack_top)
{
    /* Clear profiler structure */
    memset(prof, 0, sizeof(*prof));

    prof->sdram_total = D32_SDRAM_CAPACITY;
    prof->sdram_data_bss = data_bss_bytes;
    prof->stack_base = stack_base;
    prof->stack_top = stack_top;
    prof->fps = 60;
    prof->frame_time_ms = 16;
    prof->dram_fb_used = 320 * 224;

    /* Calculate pristine initial free SDRAM headroom */
    /* Branch condition: calculate free headroom when stack and BSS bounds are valid */
    if (stack_top > stack_base && stack_top > data_bss_bytes) {
        prof->sdram_free = stack_top - stack_base;
    } else {
        /* Fallback assumption when custom memory map boundaries are omitted */
        prof->sdram_free = (D32_SDRAM_CAPACITY > data_bss_bytes) ? (D32_SDRAM_CAPACITY - data_bss_bytes) : 0;
    }
}

/**
 * @brief Paints unallocated stack region with a canary pattern to track maximum stack depth.
 *
 * Fills memory sequentially with canary words between BSS end and active stack.
 *
 * @param start Start address of word-aligned region to paint.
 * @param end End address (exclusive) of word-aligned region to paint.
 * @param canary 32-bit canary pattern (typically D32_STACK_PAINT_CANARY).
 */
void d32_profile_paint_stack(uint32_t *start, uint32_t *end, uint32_t canary)
{
    /* Validate pointer ordering before writing to memory */
    if (!start || !end || start >= end) {
        return;
    }

    uint32_t *p = start;
    while (p < end) {
        *p++ = canary;
    }
}

/**
 * @brief Scans painted stack region to determine maximum downward stack penetration.
 *
 * Walks memory from bottom to top until finding the first word modified by stack execution.
 *
 * @param start Start address of painted stack region (typically above BSS end).
 * @param end End address of painted stack region (below active stack frame).
 * @param canary Expected canary word pattern.
 * @param stack_top Top of stack boundary.
 * @return Total bytes consumed from stack top at highest watermark.
 */
uint32_t d32_profile_scan_stack_watermark(const uint32_t *start, const uint32_t *end, uint32_t canary, uint32_t stack_top)
{
    /* Validate input pointer boundaries */
    if (!start || !end || start >= end || stack_top == 0) {
        return 0;
    }

    const uint32_t *p = start;
    /* Walk upward from lowest address toward stack top to find deepest downward stack penetration */
    while (p < end) {
        /* Branch condition: check whether stack execution overwrote the canary word */
        if (*p != canary) {
            uint32_t lowest_sp = (uint32_t)(uintptr_t)p;
            /* Branch condition: guard against stack underflow */
            if (stack_top >= lowest_sp) {
                return stack_top - lowest_sp;
            }
            return 0;
        }
        p++;
    }

    /* Stack never descended into the painted arena */
    return 0;
}

/**
 * @brief Increments cartridge bank swap event counter for telemetry.
 *
 * @param prof Target profiler instance.
 */
void d32_profile_record_bank_swap(D32ProfileTelemetry *prof)
{
    if (prof) {
        prof->bank_swap_counter++;
    }
}

/**
 * @brief Begins frame telemetry accounting.
 *
 * @param prof Target profiler instance.
 * @param frame_num Monotonically increasing frame index.
 */
void d32_profile_frame_start(D32ProfileTelemetry *prof, uint32_t frame_num)
{
    if (!prof) {
        return;
    }

    /* Every 60 frames (1 second), latch bank swaps per second */
    /* Branch condition: update 1-second rolling bank swap telemetry metric */
    if ((frame_num % 60) == 0) {
        prof->bank_swaps_per_sec = (uint16_t)prof->bank_swap_counter;
        prof->bank_swap_counter = 0;
    }
}

/**
 * @brief Computes integer CPU load percentage given active cycles and frame budget.
 *
 * Avoids floating-point math; calculates ratio clamped strictly to 0..100.
 *
 * @param active_cycles Measured execution cycles consumed in current frame.
 * @param budget_cycles Nominal 60 Hz frame cycle budget for the target CPU.
 * @return Integer percentage clamped to 0..100.
 */
uint8_t d32_profile_calc_load(uint32_t active_cycles, uint32_t budget_cycles)
{
    /* Zero budget protection */
    if (budget_cycles == 0) {
        return 0;
    }

    /* Compute integer percentage */
    uint64_t pct = ((uint64_t)active_cycles * 100u) / budget_cycles;

    /* Clamp maximum to 100% when workload overflows frame deadline */
    /* Branch condition: saturation clamp to prevent integer overflow */
    if (pct > 100u) {
        return 100;
    }

    return (uint8_t)pct;
}

/**
 * @brief Concludes frame telemetry accounting, calculating CPU loads and rolling averages.
 *
 * @param prof Target profiler instance.
 * @param msh2_cyc Active Master SH-2 cycles.
 * @param ssh2_cyc Active Slave SH-2 cycles.
 * @param m68k_cyc Active 68000 cycles.
 * @param z80_cyc Active Z80 cycles.
 * @param vbl_elapsed Elapsed VBlank count since previous frame.
 */
void d32_profile_frame_end(D32ProfileTelemetry *prof, uint32_t msh2_cyc, uint32_t ssh2_cyc, uint32_t m68k_cyc, uint32_t z80_cyc, uint16_t vbl_elapsed)
{
    if (!prof) {
        return;
    }

    /* Record raw cycle consumption */
    prof->master_sh2_cycles = msh2_cyc;
    prof->slave_sh2_cycles  = ssh2_cyc;
    prof->m68k_cycles       = m68k_cyc;
    prof->z80_cycles        = z80_cyc;

    /* Compute per-processor load percentage of nominal 16.67ms frame budget */
    prof->master_sh2_load_pct = d32_profile_calc_load(msh2_cyc, D32_FRAME_BUDGET_MSH2_CYCLES);
    prof->slave_sh2_load_pct  = d32_profile_calc_load(ssh2_cyc, D32_FRAME_BUDGET_SSH2_CYCLES);
    prof->m68k_load_pct       = d32_profile_calc_load(m68k_cyc, D32_FRAME_BUDGET_M68K_CYCLES);
    prof->z80_load_pct        = d32_profile_calc_load(z80_cyc, D32_FRAME_BUDGET_Z80_CYCLES);

    /* Rolling average frame rate and frame time over 30-frame window */
    prof->frame_accum_count++;
    prof->frame_accum_vbl += (vbl_elapsed ? vbl_elapsed : 1);

    /* Branch condition: update rolling FPS averages when accumulation window fills */
    if (prof->frame_accum_count >= 30) {
        prof->fps = (uint16_t)((prof->frame_accum_count * 60) / prof->frame_accum_vbl);
        prof->frame_time_ms = (uint16_t)((prof->frame_accum_vbl * 1000) / (prof->frame_accum_count * 60));
        prof->frame_accum_count = 0;
        prof->frame_accum_vbl = 0;
    }

    /* Update memory utilization metrics */
    uint32_t total_used = prof->sdram_data_bss + prof->stack_watermark;
    /* Branch condition: calculate free SDRAM headroom safely without negative wrap */
    if (prof->sdram_total > total_used) {
        prof->sdram_free = prof->sdram_total - total_used;
    } else {
        prof->sdram_free = 0;
    }
}

/**
 * @brief Helper drawing a compact horizontal percentage load bar into the framebuffer.
 *
 * @param fb Framebuffer pointer.
 * @param w Framebuffer width.
 * @param h Framebuffer height.
 * @param x Origin X coordinate.
 * @param y Origin Y coordinate.
 * @param bar_w Total width of load bar in pixels.
 * @param pct Percentage (0..100).
 */
static void draw_load_bar(uint8_t *fb, int w, int h, int x, int y, int bar_w, uint8_t pct)
{
    /* Color coding: Green (<70%), Gold/Yellow (70-90%), Red/Alert (>90%) */
    uint8_t bar_color = (pct > 90) ? 139 : ((pct > 70) ? 167 : 194);
    int fill_w = (bar_w * (int)pct) / 100;

    for (int py = 0; py < 3; py++) {
        int row = (y + py) * w;
        for (int px = 0; px < bar_w; px++) {
            int cx = x + px;
            /* Bounds check before writing pixel */
            if (cx >= 0 && cx < w && (y + py) >= 0 && (y + py) < h) {
                /* Branch condition: fill active load region vs empty trough */
                if (px < fill_w) {
                    fb[row + cx] = bar_color;
                } else {
                    fb[row + cx] = 151; /* Dark trough border */
                }
            }
        }
    }
}

/**
 * @brief Renders the visual performance and hardware telemetry overlay onto the framebuffer.
 *
 * Renders cycle-accurate FPS, processor load gauges, and SDRAM memory status
 * inside a stone panel overlay in the upper-right corner.
 *
 * @param fb Linear destination framebuffer pointer.
 * @param w Framebuffer width.
 * @param h Framebuffer height.
 * @param prof Active telemetry data snapshot.
 */
void d32_profile_draw_overlay(uint8_t *fb, int w, int h, const D32ProfileTelemetry *prof)
{
    if (!fb || !prof || w < 240 || h < 100) {
        return;
    }

    const int panel_w = 210;
    const int panel_h = 44;
    const int panel_x = w - panel_w - 4;
    const int panel_y = 4;

    /* Draw dark stone backdrop panel with border (zero allocation) */
    d32_draw_stone_panel(fb, w, h, panel_x, panel_y, panel_w, panel_h, 0, 151, 194, 0);

    /* Line 1: Real-time FPS, frame duration in ms, and explicit Master/Slave SH-2 load % */
    char line1[64];
    snprintf(line1, sizeof(line1), "FPS:%d %dMS MSH2:%d%% SSH2:%d%%",
             (int)prof->fps, (int)prof->frame_time_ms,
             (int)prof->master_sh2_load_pct, (int)prof->slave_sh2_load_pct);
    d32_draw_text_gold(fb, w, h, panel_x + 6, panel_y + 4, line1);

    /* Horizontal load bars for MSH2 and SSH2 aligned cleanly to the right of Line 1 text */
    draw_load_bar(fb, w, h, panel_x + 132, panel_y + 5, 32, prof->master_sh2_load_pct);
    draw_load_bar(fb, w, h, panel_x + 170, panel_y + 5, 32, prof->slave_sh2_load_pct);

    /* Line 2: 68000 and Z80 load percentages, plus bank swaps per second */
    char line2[64];
    snprintf(line2, sizeof(line2), "68K:%d%%  Z80:%d%%  SWAP:%d/S",
             (int)prof->m68k_load_pct, (int)prof->z80_load_pct,
             (int)prof->bank_swaps_per_sec);
    d32_draw_text(fb, w, h, panel_x + 6, panel_y + 17, line2, 194);
    draw_load_bar(fb, w, h, panel_x + 132, panel_y + 18, 32, prof->m68k_load_pct);
    draw_load_bar(fb, w, h, panel_x + 170, panel_y + 18, 32, prof->z80_load_pct);

    /* Line 3: 32X SDRAM allocation status (Used / 256K, Free headroom, Stack watermark) */
    char line3[64];
    uint32_t used_k = (prof->sdram_data_bss + prof->stack_watermark + 1023) / 1024;
    uint32_t free_k = prof->sdram_free / 1024;
    uint32_t stk_k  = (prof->stack_watermark + 1023) / 1024;
    snprintf(line3, sizeof(line3), "RAM:%d/256K F:%dK S:%dK",
             (int)used_k, (int)free_k, (int)stk_k);
    d32_draw_text_gold(fb, w, h, panel_x + 6, panel_y + 30, line3);
}
