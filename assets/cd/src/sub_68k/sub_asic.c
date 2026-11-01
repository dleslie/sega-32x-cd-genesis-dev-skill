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
 * @brief Populates Stamp 0 (blank), Stamp 1 (asphalt/road), Stamp 2 (curb/shoulder),
 * Stamp 3 (neon billboard), and Stamp 4 (cyber pillar/streetlight) into Word RAM.
 */
static void init_stamp_graphics(void)
{
    volatile uint32_t *stamp_ram = (volatile uint32_t *)(WRAM_1M_BASE + WRAM_STAMP_TILES_OFFSET);

    /* 1. Stamp 0: MUST be all zeros per Sega CD hardware specification (128 bytes = 32 dwords) */
    for (int i = 0; i < 32; i++)
    {
        stamp_ram[i] = 0x00000000;
    }

    /* 2. Stamp 1: Road Asphalt with Center Dashed Line (128 bytes = 32 dwords) */
    /* Color 1 = Dark asphalt, Color 2 = Road grid accent, Color 3 = Neon cyan dashed stripe */
    static const uint32_t stamp1_road[32] = {
        /* Tile 0: Top-Left (8x8) */
        0x11111111, 0x11111111, 0x11111111, 0x11111113,
        0x11111113, 0x11111111, 0x11111111, 0x11111111,
        /* Tile 1: Bottom-Left (8x8) */
        0x11111111, 0x11111111, 0x11111111, 0x11111111,
        0x11111111, 0x11111111, 0x11111111, 0x11111111,
        /* Tile 2: Top-Right (8x8) */
        0x31111111, 0x31111111, 0x11111111, 0x11111111,
        0x11111111, 0x11111111, 0x11111111, 0x11111111,
        /* Tile 3: Bottom-Right (8x8) */
        0x11111111, 0x11111111, 0x11111111, 0x11111111,
        0x11111111, 0x11111111, 0x11111111, 0x11111111
    };

    /* 3. Stamp 2: Road Shoulder / Hazard Curb Strip (Alternating Magenta / Emerald) */
    static const uint32_t stamp2_curb[32] = {
        /* Tile 0: Top-Left (8x8) */
        0x22221111, 0x22221111, 0x22221111, 0x22221111,
        0x22221111, 0x22221111, 0x22221111, 0x22221111,
        /* Tile 1: Bottom-Left (8x8) */
        0x33331111, 0x33331111, 0x33331111, 0x33331111,
        0x33331111, 0x33331111, 0x33331111, 0x33331111,
        /* Tile 2: Top-Right (8x8) */
        0x11112222, 0x11112222, 0x11112222, 0x11112222,
        0x11112222, 0x11112222, 0x11112222, 0x11112222,
        /* Tile 3: Bottom-Right (8x8) */
        0x11113333, 0x11113333, 0x11113333, 0x11113333,
        0x11113333, 0x11113333, 0x11113333, 0x11113333
    };

    /* 4. Stamp 3: Roadside Neon Billboard Object */
    static const uint32_t stamp3_billboard[32] = {
        /* Tile 0: Top-Left (8x8) */
        0x00222222, 0x00233332, 0x00230032, 0x00230032,
        0x00233332, 0x00222222, 0x00002000, 0x00002000,
        /* Tile 1: Bottom-Left (8x8) */
        0x00002000, 0x00002000, 0x00002000, 0x00002000,
        0x00002000, 0x00022200, 0x00222220, 0x11111111,
        /* Tile 2: Top-Right (8x8) */
        0x22222200, 0x23333200, 0x23003200, 0x23003200,
        0x23333200, 0x22222200, 0x00020000, 0x00020000,
        /* Tile 3: Bottom-Right (8x8) */
        0x00020000, 0x00020000, 0x00020000, 0x00020000,
        0x00020000, 0x00222000, 0x02222200, 0x11111111
    };

    /* 5. Stamp 4: Roadside Cyber Light Post / Pylon */
    static const uint32_t stamp4_lightpost[32] = {
        /* Tile 0: Top-Left (8x8) */
        0x00000033, 0x00000333, 0x00003330, 0x00000030,
        0x00000030, 0x00000030, 0x00000030, 0x00000030,
        /* Tile 1: Bottom-Left (8x8) */
        0x00000030, 0x00000030, 0x00000030, 0x00000030,
        0x00000030, 0x00000330, 0x00003333, 0x11111111,
        /* Tile 2: Top-Right (8x8) */
        0x33000000, 0x33300000, 0x03330000, 0x03000000,
        0x03000000, 0x03000000, 0x03000000, 0x03000000,
        /* Tile 3: Bottom-Right (8x8) */
        0x03000000, 0x03000000, 0x03000000, 0x03000000,
        0x03000000, 0x03300000, 0x33330000, 0x11111111
    };

    volatile uint32_t *stamp_dst;
    stamp_dst = (volatile uint32_t *)(WRAM_1M_BASE + WRAM_STAMP_TILES_OFFSET + 128 * 1);
    for (int i = 0; i < 32; i++) stamp_dst[i] = stamp1_road[i];

    stamp_dst = (volatile uint32_t *)(WRAM_1M_BASE + WRAM_STAMP_TILES_OFFSET + 128 * 2);
    for (int i = 0; i < 32; i++) stamp_dst[i] = stamp2_curb[i];

    stamp_dst = (volatile uint32_t *)(WRAM_1M_BASE + WRAM_STAMP_TILES_OFFSET + 128 * 3);
    for (int i = 0; i < 32; i++) stamp_dst[i] = stamp3_billboard[i];

    stamp_dst = (volatile uint32_t *)(WRAM_1M_BASE + WRAM_STAMP_TILES_OFFSET + 128 * 4);
    for (int i = 0; i < 32; i++) stamp_dst[i] = stamp4_lightpost[i];
}

/**
 * @brief Populates the 16x16 stamp map in Word RAM with an infinite banked road
 * and roadside objects (neon billboards and cyber light posts).
 */
static void init_stamp_map(void)
{
    volatile uint16_t *map_ram = (volatile uint16_t *)(WRAM_1M_BASE + WRAM_STAMP_MAP_OFFSET);

    /* 16x16 grid = 256 metatile references (512 bytes)
     * Layout:
     * - Columns 0..2: Left landscape / empty (Stamp 0)
     * - Column 3: Left roadside objects (Alternating Stamp 3 billboard, Stamp 4 lightpost, Stamp 0)
     * - Column 4: Left road shoulder/curb (Stamp 2)
     * - Columns 5..10: Main Road highway lanes (Stamp 1)
     * - Column 11: Right road shoulder/curb (Stamp 2)
     * - Column 12: Right roadside objects (Alternating Stamp 4 lightpost, Stamp 3 billboard, Stamp 0)
     * - Columns 13..15: Right landscape / empty (Stamp 0)
     */
    for (int row = 0; row < 16; row++)
    {
        for (int col = 0; col < 16; col++)
        {
            uint16_t stamp_id = 0;

            if (col >= 5 && col <= 10)
            {
                stamp_id = 1; /* Highway lane */
            }
            else if (col == 4 || col == 11)
            {
                stamp_id = 2; /* Road shoulder / curb */
            }
            else if (col == 3)
            {
                /* Left roadside objects */
                if ((row % 4) == 0) stamp_id = 3;      /* Neon Billboard */
                else if ((row % 4) == 2) stamp_id = 4; /* Lightpost */
                else stamp_id = 0;
            }
            else if (col == 12)
            {
                /* Right roadside objects */
                if ((row % 4) == 1) stamp_id = 4;      /* Lightpost */
                else if ((row % 4) == 3) stamp_id = 3; /* Neon Billboard */
                else stamp_id = 0;
            }
            else
            {
                stamp_id = 0; /* Off-road ground */
            }

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

/**
 * @brief Renders the pseudo-3D curved/banked road line-scrolling effect with roadside objects.
 *
 * @param road_pos_z  Current forward camera travel position (0..255 map wrap).
 * @param steer_x     Horizontal camera displacement (-128..127).
 * @param curve_amt   Active curvature deflection coefficient (-128..127).
 * @param bank_angle  Active road banking tilt angle index (0..255).
 */
void sub_asic_render_banked_road(uint16_t road_pos_z, int16_t steer_x, int16_t curve_amt, uint8_t bank_angle)
{
    /* Ensure previous ASIC render pass has concluded */
    sub_asic_wait_idle();

    volatile SubAsicTraceEntry *trace = (volatile SubAsicTraceEntry *)(WRAM_1M_BASE + WRAM_TRACE_TABLE_OFFSET);

    int16_t sin_bank = sub_sin(bank_angle);
    int16_t cos_bank = sub_cos(bank_angle);

    /* Road horizon begins at scanline HORIZON_Y (top 16 scanlines blanked for skyline) */
    const int HORIZON_Y = 16;
    const int ROAD_LINES = ASIC_SCREEN_HEIGHT - HORIZON_Y; /* 112 lines */
    const int32_t half_w = ASIC_SCREEN_WIDTH / 2;          /* 80 pixels */

    /* Scanlines 0..HORIZON_Y - 1: blank / zero-step to leave upper skyline clean */
    for (int y = 0; y < HORIZON_Y; y++)
    {
        trace[y].start_x = 0;
        trace[y].start_y = 0;
        trace[y].delta_x = 0;
        trace[y].delta_y = 0;
    }

    /* Scanlines HORIZON_Y..ASIC_SCREEN_HEIGHT - 1: perspective road projection */
    for (int i = 0; i < ROAD_LINES; i++)
    {
        int y = HORIZON_Y + i;

        /* z distance from camera: z=ROAD_LINES (far horizon) down to z=1 (bumper) */
        int32_t z = ROAD_LINES - i;
        if (z < 1) z = 1;

        /* Perspective scaling: step_ratio scales with z (sampling step across road) */
        int32_t step_ratio = (z * 2048) / 32;
        if (step_ratio < 64) step_ratio = 64;

        /* Apply banking rotation to horizontal step vector */
        int32_t delta_x = ((int32_t)cos_bank * step_ratio) >> 11;
        int32_t delta_y = ((int32_t)sin_bank * step_ratio) >> 11;

        /* Road center displacement: camera steer_x + pseudo-3D quadratic curve offset */
        int32_t curve_offset = ((int32_t)curve_amt * (i * i)) / 1024;
        int32_t road_center_x = 128 + steer_x + curve_offset;

        /* Forward world-space coordinate (wrap within 256 metatile map) */
        int32_t road_world_y = (road_pos_z + (i * 2)) & 0xFF;

        /* Initial horizontal/vertical sampling coordinates in 5.11 fixed-point */
        int32_t start_x_fp11 = (road_center_x * 2048) - (half_w * delta_x);
        int32_t start_y_fp11 = (road_world_y * 2048)  - (half_w * delta_y);

        /* Store trace entry in 13.3 format */
        trace[y].start_x = (int16_t)(start_x_fp11 >> 8);
        trace[y].start_y = (int16_t)(start_y_fp11 >> 8);
        trace[y].delta_x = (int16_t)delta_x;
        trace[y].delta_y = (int16_t)delta_y;
    }

    /* Configure ASIC Hardware Control Registers */
    SUB_REG_ASIC_STAMP_SIZE = ASIC_MAP_SIZE_256 | ASIC_STAMP_SIZE_16 | ASIC_REPEAT_WRAP;
    SUB_REG_ASIC_MAP_BASE   = (uint16_t)(WRAM_STAMP_MAP_OFFSET >> 2);
    SUB_REG_ASIC_VCELL_SIZE = (uint16_t)ASIC_VCELL_COUNT;
    SUB_REG_ASIC_IMG_START  = (uint16_t)(WRAM_IMAGE_BUFFER_OFFSET >> 2);
    SUB_REG_ASIC_IMG_OFFSET = 0x0000;
    SUB_REG_ASIC_H_PIXELS   = (uint16_t)ASIC_SCREEN_WIDTH;
    SUB_REG_ASIC_V_PIXELS   = (uint16_t)ASIC_SCREEN_HEIGHT;

    /* Writing to Trace Table Base Address triggers hardware rasterization */
    SUB_REG_ASIC_TRACE_BASE = (uint16_t)(WRAM_TRACE_TABLE_OFFSET >> 2);
}
