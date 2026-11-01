/**
 * @file sub_asic.c
 * @brief Sega CD Graphics ASIC Hardware Co-Processor Implementation.
 *
 * Implements hardware rotation, scaling, and affine transformation rendering
 * on the Sega CD Sub-CPU 68000 driving the ASIC coprocessor, including
 * pseudo-3D scanline-scrolled banked road projection and affine-scaled/rotated
 * roadside cyberpunk billboards and light pylons.
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

/* --- Precomputed 112-Entry Hyperbolic Perspective Depth LUT (z_dist = 2800 / (i + 12)) ---
 * Maps road scanline i (0 = horizon, 111 = camera bumper) to forward world distance.
 * Because s_persp_z[i] decreases as i increases toward the bottom of the screen,
 * adding an increasing road_pos_z causes stripes of constant world_z to move
 * downward from the horizon (i = 0) toward the viewer (i = 111).
 */
static const uint8_t s_persp_z[112] = {
    233, 215, 200, 186, 175, 164, 155, 147, 140, 133, 127, 121, 116, 112, 107, 103,
    100,  96,  93,  90,  87,  84,  82,  80,  77,  75,  73,  71,  70,  68,  66,  65,
     63,  62,  60,  59,  58,  57,  56,  54,  53,  52,  51,  50,  50,  49,  48,  47,
     46,  45,  45,  44,  43,  43,  42,  41,  41,  40,  40,  39,  38,  38,  37,  37,
     36,  36,  35,  35,  35,  34,  34,  33,  33,  32,  32,  32,  31,  31,  31,  30,
     30,  30,  29,  29,  29,  28,  28,  28,  28,  27,  27,  27,  26,  26,  26,  26,
     25,  25,  25,  25,  25,  24,  24,  24,  24,  23,  23,  23,  23,  23,  22,  22
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
    static const uint32_t stamp1_road[32] = {
        /* Tile 0: Top-Left (8x8) */
        0x11111111, 0x11111111, 0x22222222, 0x22222225,
        0x22222225, 0x22222222, 0x11111111, 0x11111111,
        /* Tile 1: Bottom-Left (8x8) */
        0x11111111, 0x11111111, 0x22222222, 0x22222222,
        0x22222222, 0x22222222, 0x11111111, 0x11111111,
        /* Tile 2: Top-Right (8x8) */
        0x52222222, 0x52222222, 0x22222222, 0x11111111,
        0x11111111, 0x22222222, 0x11111111, 0x11111111,
        /* Tile 3: Bottom-Right (8x8) */
        0x11111111, 0x11111111, 0x22222222, 0x22222222,
        0x22222222, 0x22222222, 0x11111111, 0x11111111
    };

    /* 3. Stamp 2: Road Shoulder / Hazard Curb Strip (Alternating Magenta / Cyan) */
    static const uint32_t stamp2_curb[32] = {
        /* Tile 0: Top-Left (8x8) */
        0x44488111, 0x44488111, 0x44488111, 0x44488111,
        0x44488111, 0x44488111, 0x44488111, 0x44488111,
        /* Tile 1: Bottom-Left (8x8) */
        0x33388222, 0x33388222, 0x33388222, 0x33388222,
        0x33388222, 0x33388222, 0x33388222, 0x33388222,
        /* Tile 2: Top-Right (8x8) */
        0x11188444, 0x11188444, 0x11188444, 0x11188444,
        0x11188444, 0x11188444, 0x11188444, 0x11188444,
        /* Tile 3: Bottom-Right (8x8) */
        0x22288333, 0x22288333, 0x22288333, 0x22288333,
        0x22288333, 0x22288333, 0x22288333, 0x22288333
    };

    /* 4. Stamp 3: Roadside Neon Billboard Object */
    static const uint32_t stamp3_billboard[32] = {
        /* Tile 0: Top-Left (8x8) */
        0x00444444, 0x00488884, 0x00483384, 0x00485584,
        0x00488884, 0x00444444, 0x00003800, 0x00003800,
        /* Tile 1: Bottom-Left (8x8) */
        0x00003800, 0x00003800, 0x00003800, 0x00003800,
        0x00003800, 0x00038300, 0x00388830, 0x66666666,
        /* Tile 2: Top-Right (8x8) */
        0x44444400, 0x48888400, 0x48338400, 0x48558400,
        0x48888400, 0x44444400, 0x00830000, 0x00830000,
        /* Tile 3: Bottom-Right (8x8) */
        0x00830000, 0x00830000, 0x00830000, 0x00830000,
        0x00830000, 0x00383000, 0x03888300, 0x66666666
    };

    /* 5. Stamp 4: Roadside Cyber Light Post / Pylon */
    static const uint32_t stamp4_lightpost[32] = {
        /* Tile 0: Top-Left (8x8) */
        0x00003388, 0x00035588, 0x00003338, 0x00000038,
        0x00000034, 0x00000038, 0x00000034, 0x00000038,
        /* Tile 1: Bottom-Left (8x8) */
        0x00000034, 0x00000038, 0x00000034, 0x00000038,
        0x00000338, 0x00003348, 0x00033888, 0x66666666,
        /* Tile 2: Top-Right (8x8) */
        0x88330000, 0x88553000, 0x83330000, 0x83000000,
        0x43000000, 0x83000000, 0x43000000, 0x83000000,
        /* Tile 3: Bottom-Right (8x8) */
        0x43000000, 0x83000000, 0x43000000, 0x83000000,
        0x83300000, 0x84330000, 0x88833000, 0x66666666
    };

    volatile uint32_t *stamp_dst;
    stamp_dst = (volatile uint32_t *)(WRAM_1M_BASE + WRAM_STAMP_TILES_OFFSET + 128 * 1);
    for (int i = 0; i < 32; i++)
    {
        stamp_dst[i] = stamp1_road[i];
    }

    stamp_dst = (volatile uint32_t *)(WRAM_1M_BASE + WRAM_STAMP_TILES_OFFSET + 128 * 2);
    for (int i = 0; i < 32; i++)
    {
        stamp_dst[i] = stamp2_curb[i];
    }

    stamp_dst = (volatile uint32_t *)(WRAM_1M_BASE + WRAM_STAMP_TILES_OFFSET + 128 * 3);
    for (int i = 0; i < 32; i++)
    {
        stamp_dst[i] = stamp3_billboard[i];
    }

    stamp_dst = (volatile uint32_t *)(WRAM_1M_BASE + WRAM_STAMP_TILES_OFFSET + 128 * 4);
    for (int i = 0; i < 32; i++)
    {
        stamp_dst[i] = stamp4_lightpost[i];
    }
}

/**
 * @brief Populates the 16x16 stamp map in Word RAM with an infinite banked road
 * and roadside objects (neon billboards and cyber light posts).
 */
static void init_stamp_map(void)
{
    volatile uint16_t *map_ram = (volatile uint16_t *)(WRAM_1M_BASE + WRAM_STAMP_MAP_OFFSET);

    for (int row = 0; row < 16; row++)
    {
        for (int col = 0; col < 16; col++)
        {
            uint16_t stamp_id = 0;

            if (col >= 5 && col <= 10)
            {
                /* Highway lane */
                stamp_id = 1;
            }
            else if (col == 4 || col == 11)
            {
                /* Road shoulder / curb */
                stamp_id = 2;
            }
            else if (col == 3)
            {
                /* Left roadside objects */
                if ((row % 4) == 0)
                {
                    stamp_id = 3;
                }
                else if ((row % 4) == 2)
                {
                    stamp_id = 4;
                }
            }
            else if (col == 12)
            {
                /* Right roadside objects */
                if ((row % 4) == 1)
                {
                    stamp_id = 4;
                }
                else if ((row % 4) == 3)
                {
                    stamp_id = 3;
                }
            }

            map_ram[(row * 16) + col] = stamp_id;
        }
    }
}

/**
 * @brief Initializes the Sega CD ASIC structures in the active Word RAM bank.
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
    /* Poll bit 15 (GRON) with safety counter; cleared to 0 when hardware graphics engine is idle */
    uint16_t timeout = 2000;
    while ((SUB_REG_ASIC_STAMP_SIZE & ASIC_FLAG_BUSY_GRON) && --timeout)
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



static uint8_t s_line_buf[160];

/**
 * @brief Renders the pseudo-3D curved/banked road line-scrolling effect.
 *
 * Populates the hardware ASIC trace vector table (0x0C0600), triggers the hardware ASIC,
 * and rasterizes the line-scrolled banked road and curbs directly into Word RAM.
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

    /* Ensure the currently active 1M Word RAM ping-pong bank has its stamps initialized */
    volatile uint16_t *map_ram = (volatile uint16_t *)(WRAM_1M_BASE + WRAM_STAMP_MAP_OFFSET);
    if (map_ram[5] == 0)
    {
        sub_asic_init();
    }

    volatile SubAsicTraceEntry *trace = (volatile SubAsicTraceEntry *)(WRAM_1M_BASE + WRAM_TRACE_TABLE_OFFSET);

    int16_t sin_bank = sub_sin(bank_angle);
    int16_t cos_bank = sub_cos(bank_angle);

    /* Road horizon begins at scanline HORIZON_Y (top 16 scanlines blanked for skyline) */
    const int HORIZON_Y = 16;
    const int ROAD_LINES = ASIC_SCREEN_HEIGHT - HORIZON_Y; /* 112 lines */
    const int32_t half_w = ASIC_SCREEN_WIDTH / 2;          /* 80 pixels */

    /* 1. Blank upper skyline trace table entries (scanlines 0..15) */
    for (int y = 0; y < HORIZON_Y; y++)
    {
        trace[y].start_x = 0;
        trace[y].start_y = 0;
        trace[y].delta_x = 0;
        trace[y].delta_y = 0;
    }

    /* 2. Configure ASIC Hardware Control Registers */
    SUB_REG_ASIC_STAMP_SIZE = ASIC_MAP_SIZE_256 | ASIC_STAMP_SIZE_16 | ASIC_REPEAT_WRAP;
    SUB_REG_ASIC_MAP_BASE   = (uint16_t)(WRAM_STAMP_MAP_OFFSET >> 2);
    SUB_REG_ASIC_VCELL_SIZE = (uint16_t)ASIC_VCELL_COUNT;
    SUB_REG_ASIC_IMG_START  = (uint16_t)(WRAM_IMAGE_BUFFER_OFFSET >> 2);
    SUB_REG_ASIC_IMG_OFFSET = 0x0000;
    SUB_REG_ASIC_H_PIXELS   = (uint16_t)ASIC_SCREEN_WIDTH;
    SUB_REG_ASIC_V_PIXELS   = (uint16_t)ASIC_SCREEN_HEIGHT;

    /* 3. Word RAM tile buffer */
    volatile uint16_t *wram_words = (volatile uint16_t *)(WRAM_1M_BASE + WRAM_IMAGE_BUFFER_OFFSET);

    /* Rows 0..1 (top 16 scanlines) are transparent so Plane A skyline shows through */
    for (int t = 0; t < 640; t++)
    {
        wram_words[t] = 0x0000;
    }

    /* 4. Single-pass combined trace generation and scanline road rasterization */
    for (int i = 0; i < ROAD_LINES; i++)
    {
        int y = HORIZON_Y + i;
        uint16_t ty = (uint16_t)y >> 3;
        uint16_t py = (uint16_t)y & 7;
        uint32_t base_word_idx = (((ty * 20U) << 3) + py) << 1;

        uint8_t z_dist = s_persp_z[i];
        uint8_t wz = (uint8_t)(z_dist + road_pos_z);
        int16_t hw = (int16_t)(16 + ((i * 54) >> 6));
        int16_t cw = (int16_t)(3 + (i >> 4));

        int32_t dz = (ROAD_LINES - 1) - i;
        int32_t curve_shift = ((int32_t)curve_amt * (dz * dz)) >> 13;
        int32_t steer_shift = ((int32_t)steer_x * (i + 16)) >> 7;
        int16_t cx = (int16_t)(80 + curve_shift + steer_shift);

        /* Populate hardware ASIC trace entry */
        int32_t step_ratio = ((int32_t)z_dist * 2048) >> 5;
        if (step_ratio < 64) step_ratio = 64;
        int32_t delta_x = ((int32_t)cos_bank * step_ratio) >> 11;
        int32_t delta_y = ((int32_t)sin_bank * step_ratio) >> 11;

        int32_t map_cx = 128 - curve_shift - steer_shift;
        int32_t start_x_fp11 = (map_cx * 2048) - (half_w * delta_x);
        int32_t start_y_fp11 = ((int32_t)wz * 2048) - (half_w * delta_y);

        trace[y].start_x = (int16_t)(start_x_fp11 >> 8);
        trace[y].start_y = (int16_t)(start_y_fp11 >> 8);
        trace[y].delta_x = (int16_t)delta_x;
        trace[y].delta_y = (int16_t)delta_y;

        if (i == 0 || i == 1)
        {
            uint16_t h_col = (i == 0) ? 0x3333 : 0x4444;
            for (int tx = 0; tx < 20; tx++)
            {
                uint32_t widx = base_word_idx + ((tx << 3) << 1);
                wram_words[widx]     = h_col;
                wram_words[widx + 1] = h_col;
            }
            continue;
        }

        bool stripe_on = ((wz & 16) == 0);
        bool band_alt  = ((wz & 8) != 0);

        uint8_t grid_col = ((wz & 15) == 0) ? 7 : 6;
        uint8_t road_col = ((wz & 7) == 0) ? 2 : (band_alt ? 2 : 1);
        uint8_t curb_col = band_alt ? 3 : 4;

        uint16_t grid_w = (uint16_t)((grid_col << 12) | (grid_col << 8) | (grid_col << 4) | grid_col);
        uint16_t road_w = (uint16_t)((road_col << 12) | (road_col << 8) | (road_col << 4) | road_col);

        int16_t l_curb = cx - hw - cw;
        int16_t l_road = cx - hw;
        int16_t r_road = cx + hw;
        int16_t r_curb = cx + hw + cw;

        /* Clamp road boundaries strictly within the 160-pixel viewport [8, 151].
         * Outer tiles tx=0 (pixels 0..7) and tx=19 (pixels 152..159) are repeated across
         * Plane B columns 0..9 and 30..39, so they must ALWAYS remain 100% off-road grid. */
        if (l_curb < 8)   l_curb = 8;
        if (l_road < 8)   l_road = 8;
        if (r_road > 151) r_road = 151;
        if (r_curb > 151) r_curb = 151;
        if (l_road > r_road) l_road = r_road;
        if (l_curb > l_road) l_curb = l_road;
        if (r_curb < r_road) r_curb = r_road;

        uint16_t l_curb_tx = (uint16_t)l_curb >> 3;
        uint16_t r_curb_tx = (uint16_t)r_curb >> 3;

        /* Initialize line buffer in the active tile span with the exact scanline grid color.
         * This eliminates stale curb/specular artifacts and ensures seamless synthwave lines. */
        int start_clear = l_curb_tx << 3;
        int end_clear = (r_curb_tx + 1) << 3;
        if (start_clear < 8)   start_clear = 8;
        if (end_clear > 152)   end_clear = 152;
        for (int x = start_clear; x < end_clear; x++)
        {
            s_line_buf[x] = grid_col;
        }

        /* Populate road curbs and asphalt in s_line_buf */
        for (int x = l_curb; x < l_road; x++)
        {
            s_line_buf[x] = curb_col;
        }
        if (cw >= 2 && (l_curb + (cw >> 1)) < l_road)
        {
            s_line_buf[l_curb + (cw >> 1)] = 8; /* White curb specular */
        }

        for (int x = l_road; x < r_road; x++)
        {
            s_line_buf[x] = road_col;
        }

        for (int x = r_road; x < r_curb; x++)
        {
            s_line_buf[x] = curb_col;
        }
        if (cw >= 2 && (r_road + (cw >> 1)) < r_curb)
        {
            s_line_buf[r_road + (cw >> 1)] = 8; /* White curb specular */
        }

        /* Center dashed dividing line (strictly within asphalt surface) */
        if (stripe_on && cx >= l_road && cx < r_road)
        {
            s_line_buf[cx] = 8;
            if (cx > l_road)     s_line_buf[cx - 1] = 5;
            if (cx < r_road - 1) s_line_buf[cx + 1] = 5;
        }

        /* Multi-lane highway dividers (strictly within asphalt surface) */
        if (stripe_on && i > 12)
        {
            int16_t lane_w = hw >> 1;
            int16_t lx = cx - lane_w;
            int16_t rx = cx + lane_w;
            if (lx >= l_road && lx < r_road) s_line_buf[lx] = 3;
            if (rx >= l_road && rx < r_road) s_line_buf[rx] = 3;
        }
        uint16_t l_road_tx = (uint16_t)(l_road + 7) >> 3;
        uint16_t r_road_tx = (uint16_t)r_road >> 3;

        for (int tx = 0; tx < 20; tx++)
        {
            uint32_t widx = base_word_idx + ((tx << 3) << 1);

            if (tx < l_curb_tx || tx > r_curb_tx)
            {
                /* Solid off-road synthwave grid tile */
                wram_words[widx]     = grid_w;
                wram_words[widx + 1] = grid_w;
            }
            else if (!stripe_on && tx >= l_road_tx && tx < r_road_tx)
            {
                /* Solid asphalt road tile without stripes */
                wram_words[widx]     = road_w;
                wram_words[widx + 1] = road_w;
            }
            else
            {
                /* Detailed tile containing curbs, stripes, or dividers */
                const uint8_t *p = &s_line_buf[tx << 3];
                uint16_t w0 = (uint16_t)((((p[0] << 4) | p[1]) << 8) | ((p[2] << 4) | p[3]));
                uint16_t w1 = (uint16_t)((((p[4] << 4) | p[5]) << 8) | ((p[6] << 4) | p[7]));
                wram_words[widx]     = w0;
                wram_words[widx + 1] = w1;
            }
        }
    }
}
