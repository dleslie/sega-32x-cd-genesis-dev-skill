/**
 * @file sub_asic.c
 * @brief Sega CD Graphics ASIC Hardware Co-Processor Implementation.
 *
 * Implements hardware rotation, scaling, and affine transformation rendering
 * on the Sega CD Sub-CPU 68000 driving the ASIC coprocessor.
 *
 * Copyright (c) 2026 Dan Leslie
 * SPDX-License-Identifier: MIT
 */

#include "sub_asic.h"

/* --- Precomputed 256-Entry Fixed-Point Sine Table (5.11 format: 2048 = 1.0) --- */
static const int16_t s_sin_lut[256] = {
       0,   50,  100,  151,  201,  251,  300,  350,  399,  449,  497,  546,  594,  642,  689,  736,
     783,  830,  876,  921,  966, 1011, 1054, 1098, 1140, 1182, 1224, 1264, 1304, 1344, 1382, 1420,
    1457, 1494, 1529, 1564, 1597, 1630, 1662, 1693, 1723, 1751, 1779, 1805, 1831, 1855, 1878, 1900,
    1921, 1941, 1959, 1976, 1991, 2006, 2019, 2030, 2040, 2049, 2056, 2062, 2067, 2070, 2072, 2073,
    2073, 2073, 2072, 2070, 2067, 2062, 2056, 2049, 2040, 2030, 2019, 2006, 1991, 1976, 1959, 1941,
    1921, 1900, 1878, 1855, 1831, 1805, 1779, 1751, 1723, 1693, 1662, 1630, 1597, 1564, 1529, 1494,
    1457, 1420, 1382, 1344, 1304, 1264, 1224, 1182, 1140, 1098, 1054, 1011,  966,  921,  876,  830,
     783,  736,  689,  642,  594,  546,  497,  449,  399,  350,  300,  251,  201,  151,  100,   50,
       0,  -50, -100, -151, -201, -251, -300, -350, -399, -449, -497, -546, -594, -642, -689, -736,
    -783, -830, -876, -921, -966,-1011,-1054,-1098,-1140,-1182,-1224,-1264,-1304,-1344,-1382,-1420,
   -1457,-1494,-1529,-1564,-1597,-1630,-1662,-1693,-1723,-1751,-1779,-1805,-1831,-1855,-1878,-1900,
   -1921,-1941,-1959,-1976,-1991,-2006,-2019,-2030,-2040,-2049,-2056,-2062,-2067,-2070,-2072,-2073,
   -2073,-2073,-2072,-2070,-2067,-2062,-2056,-2049,-2040,-2030,-2019,-2006,-1991,-1976,-1959,-1941,
   -1921,-1900,-1878,-1855,-1831,-1805,-1779,-1751,-1723,-1693,-1662,-1630,-1597,-1564,-1529,-1494,
   -1457,-1420,-1382,-1344,-1304,-1264,-1224,-1182,-1140,-1098,-1054,-1011, -966, -921, -876, -830,
    -783, -736, -689, -642, -594, -546, -497, -449, -399, -350, -300, -251, -201, -151, -100,  -50
};

/**
 * @brief Evaluates fixed-point sine without branching via modulo 256 indexing.
 */
int16_t sub_sin(uint8_t angle)
{
    return s_sin_lut[angle];
}

/**
 * @brief Evaluates fixed-point cosine by advancing angle phase by 90 degrees (64 steps).
 */
int16_t sub_cos(uint8_t angle)
{
    return s_sin_lut[(uint8_t)(angle + 64)];
}

/**
 * @brief Populates Stamp 0 (blank) and Stamp 1 (16x16 decorative pattern) into Word RAM.
 */
static void init_stamp_graphics(void)
{
    volatile uint32_t *stamp_ram = (volatile uint32_t *)(WRAM_1M_BASE + WRAM_STAMP_TILES_OFFSET);

    /* 1. Stamp 0: MUST be all zeros per Sega CD hardware specification (128 bytes = 32 dwords) */
    for (int i = 0; i < 32; i++)
    {
        stamp_ram[i] = 0x00000000;
    }

    /* 2. Stamp 1: 16x16 metatile composed of 4 Genesis 8x8 tiles (128 bytes = 32 dwords) */
    /* Pattern: High-contrast target emblem with border, crosshair, and center gem */
    static const uint32_t stamp1_tiles[32] = {
        /* Tile 0: Top-Left (8x8) */
        0x11111111, 0x12222221, 0x12333321, 0x12300321,
        0x12300321, 0x12333321, 0x12222221, 0x11111111,
        /* Tile 1: Bottom-Left (8x8) */
        0x11111111, 0x12222221, 0x12333321, 0x12300321,
        0x12300321, 0x12333321, 0x12222221, 0x11111111,
        /* Tile 2: Top-Right (8x8) */
        0x11111111, 0x12222221, 0x12333321, 0x12300321,
        0x12300321, 0x12333321, 0x12222221, 0x11111111,
        /* Tile 3: Bottom-Right (8x8) */
        0x11111111, 0x12222221, 0x12333321, 0x12300321,
        0x12300321, 0x12333321, 0x12222221, 0x11111111
    };

    volatile uint32_t *stamp1_dst = (volatile uint32_t *)(WRAM_1M_BASE + WRAM_STAMP_TILES_OFFSET + 128);
    for (int i = 0; i < 32; i++)
    {
        stamp1_dst[i] = stamp1_tiles[i];
    }
}

/**
 * @brief Populates the 16x16 stamp map in Word RAM with a checkerboard layout.
 */
static void init_stamp_map(void)
{
    volatile uint16_t *map_ram = (volatile uint16_t *)(WRAM_1M_BASE + WRAM_STAMP_MAP_OFFSET);

    /* 16x16 grid = 256 metatile references (512 bytes) */
    for (int row = 0; row < 16; row++)
    {
        for (int col = 0; col < 16; col++)
        {
            /* Alternating checkerboard of Stamp 1 and Stamp 0 without branching */
            uint16_t stamp_id = (uint16_t)((row + col) & 0x0001);
            map_ram[(row * 16) + col] = stamp_id;
        }
    }
}

/**
 * @brief Initializes the Sega CD ASIC structures in Word RAM.
 */
void sub_asic_init(void)
{
    init_stamp_graphics();
    init_stamp_map();
}

/**
 * @brief Polls the ASIC status register until the active graphics operation finishes.
 */
void sub_asic_wait_idle(void)
{
    /* Poll bit 15 (GRON); cleared to 0 when hardware graphics engine is idle */
    while (SUB_REG_ASIC_STAMP_SIZE & ASIC_FLAG_BUSY_GRON)
    {
        __asm__ volatile ("nop");
    }
}

/**
 * @brief Computes affine transformation vectors and triggers the ASIC hardware rasterizer.
 *
 * @param angle_idx 8-bit rotation angle (0..255 representing 0..360 degrees).
 * @param scale_q8  8.8 fixed-point scale factor (256 = 1.0x normal size).
 */
void sub_asic_render_transform(uint8_t angle_idx, uint16_t scale_q8)
{
    /* Ensure previous ASIC render pass has concluded */
    sub_asic_wait_idle();

    /* Safety clamp: prevent divide-by-zero on scale */
    if (scale_q8 < 32)
    {
        scale_q8 = 32;
    }

    /* 1. Compute sampling step ratio in 5.11 fixed-point (2048 = 1.0x step) */
    int32_t step_ratio = (256 * 2048) / (int32_t)scale_q8;

    /* 2. Compute delta step vectors in 5.11 format */
    int16_t sin_val = sub_sin(angle_idx);
    int16_t cos_val = sub_cos(angle_idx);

    int32_t delta_x = ((int32_t)cos_val * step_ratio) >> 11;
    int32_t delta_y = ((int32_t)sin_val * step_ratio) >> 11;

    /* 3. Setup scanline recurrence around center of 256x256 stamp map (128, 128) */
    /* Center coordinates in 5.11 fixed-point (128 pixels * 2048 = 262144) */
    const int32_t center_fp11 = 128 * 2048;
    const int32_t half_w = ASIC_SCREEN_WIDTH / 2;
    const int32_t half_h = ASIC_SCREEN_HEIGHT / 2;

    /* Starting sampling coordinates for the top-left screen pixel (scanline 0) */
    int32_t cur_x_fp11 = center_fp11 - (half_w * delta_x) + (half_h * delta_y);
    int32_t cur_y_fp11 = center_fp11 - (half_w * delta_y) - (half_h * delta_x);

    /* Per-scanline perpendicular stepping increments */
    int32_t step_x_fp11 = -delta_y;
    int32_t step_y_fp11 =  delta_x;

    /* 4. Write Trace Table entries in Word RAM (one 8-byte entry per scanline) */
    volatile SubAsicTraceEntry *trace = (volatile SubAsicTraceEntry *)(WRAM_1M_BASE + WRAM_TRACE_TABLE_OFFSET);

    for (int y = 0; y < ASIC_SCREEN_HEIGHT; y++)
    {
        /* Convert 5.11 coordinates to 13.3 format (shift right by 8 bits) */
        trace[y].start_x = (int16_t)(cur_x_fp11 >> 8);
        trace[y].start_y = (int16_t)(cur_y_fp11 >> 8);
        trace[y].delta_x = (int16_t)delta_x;
        trace[y].delta_y = (int16_t)delta_y;

        /* Advance starting position to next vertical raster scanline */
        cur_x_fp11 += step_x_fp11;
        cur_y_fp11 += step_y_fp11;
    }

    /* 5. Configure ASIC Hardware Control Registers */
    SUB_REG_ASIC_STAMP_SIZE = ASIC_MAP_SIZE_256 | ASIC_STAMP_SIZE_16 | ASIC_REPEAT_WRAP;
    SUB_REG_ASIC_MAP_BASE   = (uint16_t)(WRAM_STAMP_MAP_OFFSET >> 2);
    SUB_REG_ASIC_VCELL_SIZE = (uint16_t)ASIC_VCELL_COUNT;
    SUB_REG_ASIC_IMG_START  = (uint16_t)(WRAM_IMAGE_BUFFER_OFFSET >> 2);
    SUB_REG_ASIC_IMG_OFFSET = 0x0000;
    SUB_REG_ASIC_H_PIXELS   = (uint16_t)ASIC_SCREEN_WIDTH;
    SUB_REG_ASIC_V_PIXELS   = (uint16_t)ASIC_SCREEN_HEIGHT;

    /* 6. Writing to Trace Table Base Address triggers hardware rasterization */
    SUB_REG_ASIC_TRACE_BASE = (uint16_t)(WRAM_TRACE_TABLE_OFFSET >> 2);
}
