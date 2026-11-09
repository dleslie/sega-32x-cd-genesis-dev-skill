// SPDX-License-Identifier: MIT
/**
 * @file profile.h
 * @brief Cycle-accurate CPU performance profiling and hardware memory telemetry for Sega 32X.
 *
 * Provides real-time load accounting for Master SH-2, Slave SH-2, Genesis 68000,
 * and Z80 coprocessors, alongside 32X SDRAM, Framebuffer DRAM, and cartridge
 * bank swap telemetry.
 */

#ifndef D32_PROFILE_H
#define D32_PROFILE_H

#include <stdint.h>
#include <stddef.h>

/* Cycle budgets per 60 Hz NTSC frame (16.667 ms nominal budget) */
#define D32_FRAME_BUDGET_MSH2_CYCLES  383520u   /* 23.01136 MHz / 60 */
#define D32_FRAME_BUDGET_SSH2_CYCLES  383520u   /* 23.01136 MHz / 60 */
#define D32_FRAME_BUDGET_M68K_CYCLES  127840u   /* 7.670453 MHz / 60 */
#define D32_FRAME_BUDGET_Z80_CYCLES    59660u   /* 3.579545 MHz / 60 */

#define D32_SDRAM_CAPACITY            (256 * 1024)  /* 256 KiB fixed 32X SDRAM */
#define D32_STACK_PAINT_CANARY        0x5A5A5A5Au

/**
 * @brief Comprehensive cycle-accurate CPU load and memory telemetry snapshot structure.
 */
typedef struct {
    /* CPU cycle counts per frame */
    uint32_t master_sh2_cycles;   /* Master SH-2 active execution cycles */
    uint32_t slave_sh2_cycles;    /* Slave SH-2 audio/command mixing cycles */
    uint32_t m68k_cycles;         /* 68000 bus & interrupt cycles */
    uint32_t z80_cycles;          /* Z80 audio synthesis cycles */

    /* CPU Load percentages (0..100% of 16.67ms frame budget) */
    uint8_t master_sh2_load_pct;  /* Master SH-2 % load */
    uint8_t slave_sh2_load_pct;   /* Slave SH-2 % load */
    uint8_t m68k_load_pct;        /* 68000 % load */
    uint8_t z80_load_pct;         /* Z80 % load */

    /* Sub-system phase duration breakdown in microseconds */
    uint16_t sim_time_us;         /* World simulation, AI, and collision */
    uint16_t render_time_us;      /* Master SH-2 rasterization and viewport */
    uint16_t slave_mix_time_us;   /* Slave SH-2 audio PWM mixing */
    uint16_t vblank_wait_us;      /* Idle wait duration for VBlank / flip */

    /* Memory instrumentation (bytes) */
    uint32_t sdram_total;         /* Fixed 262144 bytes */
    uint32_t sdram_data_bss;      /* Data + BSS static allocations */
    uint32_t stack_watermark;     /* High-water mark of stack usage */
    uint32_t sdram_free;          /* Free headroom between BSS and stack */
    uint32_t dram_fb_used;        /* Framebuffer bytes in active use */
    uint16_t bank_swaps_per_sec;  /* SSF bank page swaps in past second */

    /* Frame timing telemetry */
    uint16_t fps;                 /* Real-time FPS (0..60) */
    uint16_t frame_time_ms;       /* Measured frame time in milliseconds */

    /* Internal rolling accumulators */
    uint32_t bank_swap_counter;
    uint16_t frame_accum_count;
    uint16_t frame_accum_vbl;
    uint32_t stack_base;
    uint32_t stack_top;
} D32ProfileTelemetry;

/* Global telemetry instance */
extern D32ProfileTelemetry g_d32_profile;

/**
 * @brief Initializes performance profiler and memory instrumentation tracking.
 *
 * @param prof Target profiler instance.
 * @param data_bss_bytes Total size of initialized data and BSS memory in bytes.
 * @param stack_base Base address of dynamically monitored stack arena.
 * @param stack_top Top of the stack address (initial SP value, e.g. 0x0603F000).
 */
void d32_profile_init(D32ProfileTelemetry *prof, uint32_t data_bss_bytes, uint32_t stack_base, uint32_t stack_top);

/**
 * @brief Paints unallocated stack region with a canary pattern to track maximum stack depth.
 *
 * @param start Start address of word-aligned region to paint.
 * @param end End address (exclusive) of word-aligned region to paint.
 * @param canary 32-bit canary pattern (typically D32_STACK_PAINT_CANARY).
 */
void d32_profile_paint_stack(uint32_t *start, uint32_t *end, uint32_t canary);

/**
 * @brief Scans painted stack region to determine maximum downward stack penetration.
 *
 * @param start Start address of painted stack region (typically above BSS end).
 * @param end End address of painted stack region (below active stack frame).
 * @param canary Expected canary word pattern.
 * @param stack_top Top of stack boundary.
 * @return Total bytes consumed from stack top at highest watermark.
 */
uint32_t d32_profile_scan_stack_watermark(const uint32_t *start, const uint32_t *end, uint32_t canary, uint32_t stack_top);

/**
 * @brief Increments cartridge bank swap event counter for telemetry.
 *
 * @param prof Target profiler instance.
 */
void d32_profile_record_bank_swap(D32ProfileTelemetry *prof);

/**
 * @brief Begins frame telemetry accounting.
 *
 * @param prof Target profiler instance.
 * @param frame_num Monotonically increasing frame index.
 */
void d32_profile_frame_start(D32ProfileTelemetry *prof, uint32_t frame_num);

/**
 * @brief Computes integer CPU load percentage given active cycles and frame budget.
 *
 * @param active_cycles Measured execution cycles consumed in current frame.
 * @param budget_cycles Nominal 60 Hz frame cycle budget for the target CPU.
 * @return Integer percentage clamped to 0..100.
 */
uint8_t d32_profile_calc_load(uint32_t active_cycles, uint32_t budget_cycles);

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
void d32_profile_frame_end(D32ProfileTelemetry *prof, uint32_t msh2_cyc, uint32_t ssh2_cyc, uint32_t m68k_cyc, uint32_t z80_cyc, uint16_t vbl_elapsed);

/**
 * @brief Renders the visual performance and hardware telemetry overlay onto the framebuffer.
 *
 * @param fb Linear destination framebuffer pointer.
 * @param w Framebuffer width.
 * @param h Framebuffer height.
 * @param prof Active telemetry data snapshot.
 */
void d32_profile_draw_overlay(uint8_t *fb, int w, int h, const D32ProfileTelemetry *prof);

#endif /* D32_PROFILE_H */
