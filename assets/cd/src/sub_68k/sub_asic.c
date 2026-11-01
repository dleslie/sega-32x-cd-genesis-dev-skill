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

/* --- 16x16 Roadside Object & Tall Building Textures (4bpp Color Indices in PAL0) ---
 * Color 0: Transparent
 * Color 1: Dark Cyber-Steel High-Rise Facade (#22223b)
 * Color 2: Lighter Steel Structural Mullions (#38385c)
 * Color 3: Electric Cyan (#00f5d4)
 * Color 4: Neon Hot Magenta (#ff0066)
 * Color 5: Cyber Amber Gold (#ffe600)
 * Color 7: Outrun Violet Accent (#7b2cbf)
 * Color 8: Pure White Specular (#ffffff)
 */
static const uint8_t s_billboard_tex[16][16] = {
    { 0, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 0 },
    { 4, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4 },
    { 4, 8, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 4 },
    { 4, 8, 4, 3, 3, 5, 5, 3, 3, 5, 5, 3, 3, 4, 8, 4 },
    { 4, 8, 4, 3, 8, 5, 8, 3, 8, 5, 8, 3, 8, 4, 8, 4 },
    { 4, 8, 4, 3, 3, 5, 5, 3, 3, 5, 5, 3, 3, 4, 8, 4 },
    { 4, 8, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 4 },
    { 4, 8, 4, 5, 5, 5, 8, 8, 8, 8, 5, 5, 5, 4, 8, 4 },
    { 4, 8, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 8, 4 },
    { 4, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 4 },
    { 0, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 0 },
    { 0, 0, 0, 3, 8, 0, 0, 0, 0, 0, 0, 8, 3, 0, 0, 0 },
    { 0, 0, 0, 3, 8, 0, 0, 0, 0, 0, 0, 8, 3, 0, 0, 0 },
    { 0, 0, 0, 3, 8, 0, 0, 0, 0, 0, 0, 8, 3, 0, 0, 0 },
    { 0, 0, 3, 3, 8, 3, 0, 0, 0, 0, 3, 8, 3, 3, 0, 0 },
    { 0, 3, 3, 8, 8, 3, 3, 0, 0, 3, 3, 8, 8, 3, 3, 0 }
};

static const uint8_t s_pylon_tex[16][16] = {
    { 0, 0, 3, 3, 8, 8, 8, 8, 8, 8, 8, 8, 3, 3, 0, 0 },
    { 0, 3, 5, 5, 8, 8, 5, 5, 5, 5, 8, 8, 5, 5, 3, 0 },
    { 0, 0, 3, 3, 3, 3, 3, 8, 8, 3, 3, 3, 3, 3, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 3, 8, 8, 3, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 3, 4, 4, 3, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 3, 8, 8, 3, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 3, 4, 4, 3, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 3, 8, 8, 3, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 3, 4, 4, 3, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 3, 8, 8, 3, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 3, 4, 4, 3, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 3, 8, 8, 3, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 3, 3, 8, 8, 3, 3, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 3, 3, 4, 8, 8, 4, 3, 3, 0, 0, 0, 0 },
    { 0, 0, 0, 3, 3, 4, 4, 8, 8, 4, 4, 3, 3, 0, 0, 0 },
    { 0, 0, 3, 3, 8, 8, 8, 8, 8, 8, 8, 8, 3, 3, 0, 0 }
};

/* Building Variety 0: Tall Stepped Spire Skyscraper with Amber Beacon & Cyan Window Grid */
static const uint8_t s_bldg_spire_tex[16][16] = {
    { 0, 0, 0, 0, 0, 0, 0, 8, 8, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 5, 8, 8, 5, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 0, 3, 3, 2, 2, 3, 3, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 3, 1, 3, 1, 1, 3, 1, 3, 0, 0, 0, 0 },
    { 0, 0, 3, 3, 3, 3, 3, 8, 8, 3, 3, 3, 3, 3, 0, 0 },
    { 0, 0, 3, 1, 3, 1, 5, 1, 1, 5, 1, 3, 1, 3, 0, 0 },
    { 0, 0, 3, 1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 3, 0, 0 },
    { 0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0 },
    { 0, 3, 1, 3, 1, 3, 1, 2, 2, 1, 3, 1, 3, 1, 3, 0 },
    { 0, 3, 1, 1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 1, 3, 0 },
    { 0, 3, 1, 5, 1, 3, 1, 2, 2, 1, 3, 1, 5, 1, 3, 0 },
    { 0, 3, 1, 1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 1, 3, 0 },
    { 0, 3, 1, 3, 1, 5, 1, 2, 2, 1, 5, 1, 3, 1, 3, 0 },
    { 0, 3, 1, 1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 1, 3, 0 },
    { 0, 3, 1, 3, 1, 3, 1, 8, 8, 1, 3, 1, 3, 1, 3, 0 },
    { 0, 3, 2, 2, 2, 2, 3, 8, 8, 3, 2, 2, 2, 2, 3, 0 }
};

/* Building Variety 1: Megacorp High-Rise with Rooftop Hologram Sign & Magenta Windows */
static const uint8_t s_bldg_megacorp_tex[16][16] = {
    { 0, 0, 4, 4, 8, 8, 4, 4, 4, 4, 8, 8, 4, 4, 0, 0 },
    { 0, 0, 4, 5, 5, 8, 5, 4, 4, 5, 8, 5, 5, 4, 0, 0 },
    { 0, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 0 },
    { 0, 4, 1, 4, 1, 4, 1, 4, 4, 1, 4, 1, 4, 1, 4, 0 },
    { 0, 4, 1, 1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 1, 4, 0 },
    { 0, 4, 1, 4, 1, 5, 1, 4, 4, 1, 5, 1, 4, 1, 4, 0 },
    { 0, 4, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 4, 0 },
    { 0, 4, 1, 3, 3, 3, 3, 8, 8, 3, 3, 3, 3, 1, 4, 0 },
    { 0, 4, 1, 3, 8, 5, 5, 8, 8, 5, 5, 8, 3, 1, 4, 0 },
    { 0, 4, 1, 3, 3, 3, 3, 8, 8, 3, 3, 3, 3, 1, 4, 0 },
    { 0, 4, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 4, 0 },
    { 0, 4, 1, 4, 1, 4, 1, 4, 4, 1, 4, 1, 4, 1, 4, 0 },
    { 0, 4, 1, 1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 1, 4, 0 },
    { 0, 4, 1, 5, 1, 4, 1, 4, 4, 1, 4, 1, 5, 1, 4, 0 },
    { 0, 4, 1, 1, 1, 1, 4, 8, 8, 4, 1, 1, 1, 1, 4, 0 },
    { 0, 4, 2, 2, 2, 2, 4, 8, 8, 4, 2, 2, 2, 2, 4, 0 }
};

/* Building Variety 2: Cyber-Pyramid Arcology with Twin Spires & Gold/Violet Tiers */
static const uint8_t s_bldg_arcology_tex[16][16] = {
    { 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0 },
    { 0, 0, 0, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 0, 0, 0 },
    { 0, 0, 0, 5, 1, 3, 1, 8, 8, 1, 3, 1, 5, 0, 0, 0 },
    { 0, 0, 0, 5, 1, 1, 1, 2, 2, 1, 1, 1, 5, 0, 0, 0 },
    { 0, 0, 5, 5, 7, 7, 7, 8, 8, 7, 7, 7, 5, 5, 0, 0 },
    { 0, 0, 5, 1, 5, 1, 5, 2, 2, 5, 1, 5, 1, 5, 0, 0 },
    { 0, 0, 5, 1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 5, 0, 0 },
    { 0, 0, 5, 1, 3, 1, 3, 8, 8, 3, 1, 3, 1, 5, 0, 0 },
    { 0, 5, 5, 7, 7, 7, 7, 8, 8, 7, 7, 7, 7, 5, 5, 0 },
    { 0, 5, 1, 5, 1, 4, 1, 2, 2, 1, 4, 1, 5, 1, 5, 0 },
    { 0, 5, 1, 1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 1, 5, 0 },
    { 0, 5, 1, 3, 1, 5, 1, 8, 8, 1, 5, 1, 3, 1, 5, 0 },
    { 0, 5, 1, 1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 1, 5, 0 },
    { 0, 5, 1, 5, 1, 3, 5, 8, 8, 5, 3, 1, 5, 1, 5, 0 },
    { 0, 5, 2, 2, 2, 2, 5, 8, 8, 5, 2, 2, 2, 2, 5, 0 }
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
 * @brief Fast, high-precision perspective texture mapping for huge roadside skyscrapers.
 *
 * Scales and banks towering skyscrapers directly into the row-major 4bpp VDP tile buffer.
 * Uses 6.10 fixed-point stepping for razor-sharp scaling up to full screen height (110+ px).
 */
static void render_scaled_rotated_roadside_obj(
    volatile uint32_t *wram_tiles,
    int16_t center_x,
    int16_t base_y,
    int16_t width_px,
    int16_t height_px,
    int16_t cos_bank,
    int16_t sin_bank,
    uint8_t obj_type)
{
    if (width_px < 4 || height_px < 4)
    {
        return;
    }

    /* Compute 6.10 fixed-point stepping increments for crisp vertical and horizontal resolution */
    (void)cos_bank;
    (void)sin_bank;
    int32_t inv_w = (16 * 1024) / width_px;
    int32_t inv_h = (16 * 1024) / height_px;

    int16_t half_w = width_px >> 1;
    int16_t top_y  = base_y - height_px;
    int16_t left_x = center_x - half_w;

    const uint8_t (*tex)[16];
    switch (obj_type)
    {
        case 1:  tex = s_pylon_tex;         break;
        case 2:  tex = s_bldg_spire_tex;    break;
        case 3:  tex = s_bldg_megacorp_tex; break;
        case 4:  tex = s_bldg_arcology_tex; break;
        default: tex = s_billboard_tex;     break;
    }

    for (int16_t dy = 0; dy < height_px; dy++)
    {
        int16_t sy = top_y + dy;
        if ((uint16_t)sy >= ASIC_SCREEN_HEIGHT)
        {
            continue;
        }

        /* Vertical texture coordinate along skyscraper face */
        int16_t v = (int16_t)((dy * inv_h) >> 10);
        if ((uint16_t)v >= 16)
        {
            v = 15;
        }
        const uint8_t *tex_row = tex[v];

        uint16_t tile_row = (uint16_t)sy >> 3;
        uint16_t line_in_tile = (uint16_t)sy & 7;

        int32_t u_fp = 0;
        for (int16_t dx = 0; dx < width_px; dx++)
        {
            int16_t sx = left_x + dx;
            /* Clamp strictly to inner columns tx = 1..18 (sx = 8..151) so outer repeat tiles stay clean */
            if (sx >= 8 && sx < 152)
            {
                int16_t u = (int16_t)(u_fp >> 10);
                if ((uint16_t)u < 16)
                {
                    uint8_t color = tex_row[u];
                    if (color != 0)
                    {
                        uint16_t tile_col = (uint16_t)sx >> 3;
                        uint16_t px_in_tile = (uint16_t)sx & 7;
                        uint32_t shift = (uint32_t)((7 - px_in_tile) << 2);
                        uint32_t mask = ~(0x0FUL << shift);
                        uint32_t val  = ((uint32_t)color) << shift;

                        uint32_t idx = ((tile_row * 20U + tile_col) << 3) + line_in_tile;
                        wram_tiles[idx] = (wram_tiles[idx] & mask) | val;
                    }
                }
            }
            u_fp += inv_w;
        }
    }
}

/**
 * @brief Renders the pseudo-3D curved/banked road line-scrolling effect with roadside objects.
 *
 * Populates the hardware ASIC trace vector table (0x0C0600), triggers the hardware ASIC,
 * and rasterizes the line-scrolled banked road and affine-scaled/rotated roadside objects
 * into the row-major 4bpp VDP tile canvas at 0x0C1000.
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

    /* Precomputed per-scanline geometry arrays for fast row-major tile output */
    int16_t row_cx[112];
    int16_t row_half_w[112];
    int16_t row_curb_w[112];
    uint8_t row_wz[112];

    /* 1. Scanlines 0..HORIZON_Y - 1: blank trace entries for upper skyline */
    for (int y = 0; y < HORIZON_Y; y++)
    {
        trace[y].start_x = 0;
        trace[y].start_y = 0;
        trace[y].delta_x = 0;
        trace[y].delta_y = 0;
    }

    /* 2. Scanlines HORIZON_Y..ASIC_SCREEN_HEIGHT - 1: compute per-line perspective & trace table */
    for (int i = 0; i < ROAD_LINES; i++)
    {
        int y = HORIZON_Y + i;

        /* Perspective distance ahead of camera (decreases as i approaches bottom of screen) */
        uint8_t z_dist = s_persp_z[i];

        /* Forward world Z coordinate: adding road_pos_z moves fixed world features
         * toward smaller z_dist (larger i = downward toward camera!) as road_pos_z advances */
        uint8_t world_z = (uint8_t)(z_dist + road_pos_z);
        row_wz[i] = world_z;

        /* Perspective road half-width in pixels: 12px at horizon (i=0) to 95px at bottom (i=111) */
        int16_t r_half_w = (int16_t)(12 + ((i * 48) >> 6));
        int16_t r_curb_w = (int16_t)(3 + (i >> 4));
        row_half_w[i] = r_half_w;
        row_curb_w[i] = r_curb_w;

        /* Quadratic S-curve bend toward the horizon + camera lateral steering offset */
        int32_t dz = (ROAD_LINES - 1) - i;
        int32_t curve_shift = ((int32_t)curve_amt * (dz * dz)) >> 13;
        int32_t steer_shift = ((int32_t)steer_x * (i + 16)) >> 7;
        row_cx[i] = (int16_t)(80 + curve_shift + steer_shift);

        /* Populate hardware ASIC trace entry for scanline y */
        int32_t step_ratio = ((int32_t)z_dist * 2048) >> 5;
        if (step_ratio < 64)
        {
            step_ratio = 64;
        }
        int32_t delta_x = ((int32_t)cos_bank * step_ratio) >> 11;
        int32_t delta_y = ((int32_t)sin_bank * step_ratio) >> 11;

        int32_t map_cx = 128 - curve_shift - steer_shift;
        int32_t start_x_fp11 = (map_cx * 2048) - (half_w * delta_x);
        int32_t start_y_fp11 = ((int32_t)world_z * 2048) - (half_w * delta_y);

        trace[y].start_x = (int16_t)(start_x_fp11 >> 8);
        trace[y].start_y = (int16_t)(start_y_fp11 >> 8);
        trace[y].delta_x = (int16_t)delta_x;
        trace[y].delta_y = (int16_t)delta_y;
    }

    /* 3. Configure ASIC Hardware Control Registers and trigger trace rasterization */
    SUB_REG_ASIC_STAMP_SIZE = ASIC_MAP_SIZE_256 | ASIC_STAMP_SIZE_16 | ASIC_REPEAT_WRAP;
    SUB_REG_ASIC_MAP_BASE   = (uint16_t)(WRAM_STAMP_MAP_OFFSET >> 2);
    SUB_REG_ASIC_VCELL_SIZE = (uint16_t)ASIC_VCELL_COUNT;
    SUB_REG_ASIC_IMG_START  = (uint16_t)(WRAM_IMAGE_BUFFER_OFFSET >> 2);
    SUB_REG_ASIC_IMG_OFFSET = 0x0000;
    SUB_REG_ASIC_H_PIXELS   = (uint16_t)ASIC_SCREEN_WIDTH;
    SUB_REG_ASIC_V_PIXELS   = (uint16_t)ASIC_SCREEN_HEIGHT;
    SUB_REG_ASIC_TRACE_BASE = (uint16_t)(WRAM_TRACE_TABLE_OFFSET >> 2);

    /* Wait for hardware ASIC pass to complete before updating row-major VDP tile buffer */
    sub_asic_wait_idle();

    /* 4. Rasterize line-scrolled banked road into row-major 4bpp VDP tiles at 0x0C1000 */
    volatile uint32_t *wram_tiles = (volatile uint32_t *)(WRAM_1M_BASE + WRAM_IMAGE_BUFFER_OFFSET);
    int16_t bank_slope = (int16_t)(sin_bank >> 5); /* -64..+64 */

    for (uint16_t tx = 0; tx < 20; tx++)
    {
        /* Horizontal pixel center of this 8-pixel tile column (4..156) */
        int16_t xc = (int16_t)((tx << 3) + 4);

        /* Banking vertical tilt offset for this column: tilts horizon and road lines */
        int16_t bank_dy = (int16_t)((((int16_t)tx - 10) * bank_slope) >> 3);

        for (uint16_t ty = 0; ty < 16; ty++)
        {
            volatile uint32_t *tile_dst = &wram_tiles[(ty * 20U + tx) << 3];
            uint16_t base_y = ty << 3;

            for (uint16_t py = 0; py < 8; py++)
            {
                int16_t y = (int16_t)(base_y + py);
                int16_t i = y - HORIZON_Y + bank_dy;

                if (i < 0)
                {
                    /* Above banked horizon: transparent so Plane A city skyline shows through */
                    tile_dst[py] = 0x00000000;
                }
                else if (i == 0 || i == 1)
                {
                    /* Glowing electric cyan / hot pink banked horizon line */
                    tile_dst[py] = (i == 0) ? 0x33333333 : 0x44444444;
                }
                else
                {
                    int16_t ic = (i > 111) ? 111 : i;
                    uint8_t wz = row_wz[ic];

                    /* Outer tile columns (tx == 0 and tx == 19) repeat across screen columns 0..9 and 30..39
                     * on VDP Plane B; draw only clean off-road synthwave ground grid here so road curbs
                     * and billboards never repeat across the outer wings. */
                    if (tx == 0 || tx == 19)
                    {
                        tile_dst[py] = ((wz & 15) == 0) ? 0x77777777 : 0x66666666;
                        continue;
                    }

                    int16_t cx = row_cx[ic];
                    int16_t hw = row_half_w[ic];
                    int16_t cw = row_curb_w[ic];

                    int16_t dx = xc - cx;
                    int16_t abs_dx = (dx < 0) ? -dx : dx;
                    bool stripe_on = ((wz & 16) == 0);
                    bool band_alt  = ((wz & 8) != 0);

                    /* Fast path when the entire 8-pixel tile row is well outside the road + curb + reflector */
                    if (abs_dx >= (hw + cw + 10))
                    {
                        tile_dst[py] = ((wz & 15) == 0) ? 0x77777777 : 0x66666666;
                    }
                    else
                    {
                        /* Per-pixel accurate 8-nibble rasterization for crisp 1px diagonal perspective edges */
                        uint32_t row_bits = 0;
                        int16_t base_x = (int16_t)(tx << 3);
                        int16_t lane_pos = hw >> 1;
                        int16_t center_w = (int16_t)(1 + (ic >> 5));
                        int16_t lane_w   = (int16_t)(1 + (ic >> 6));
                        uint8_t curb_col = band_alt ? 3 : 4;
                        uint8_t road_col = ((wz & 7) == 0) ? 2 : (band_alt ? 2 : 1);
                        uint8_t grid_col = ((wz & 15) == 0) ? 7 : 6;

                        for (uint16_t px = 0; px < 8; px++)
                        {
                            int16_t x = (int16_t)(base_x + px);
                            int16_t pdx = x - cx;
                            int16_t pabs = (pdx < 0) ? -pdx : pdx;
                            uint8_t col;

                            if (pabs < hw)
                            {
                                int16_t ldiff = pabs - lane_pos;
                                if (ldiff < 0)
                                {
                                    ldiff = -ldiff;
                                }

                                if (stripe_on && pabs <= center_w)
                                {
                                    /* Glowing Cyber-Gold dashed center line with white-hot core */
                                    col = (pabs == 0) ? 8 : 5;
                                }
                                else if (stripe_on && ic > 10 && ldiff <= lane_w)
                                {
                                    /* Electric Cyan dashed lane divider */
                                    col = 3;
                                }
                                else
                                {
                                    col = road_col;
                                }
                            }
                            else if (pabs < (hw + cw))
                            {
                                /* Road Shoulder / Hazard Rumble Curb: alternating Neon Cyan & Magenta with white highlight */
                                int16_t cpos = pabs - hw;
                                col = (cpos == (cw >> 1)) ? 8 : curb_col;
                            }
                            else if (stripe_on && pabs < (hw + cw + 3))
                            {
                                /* Roadside neon reflector strip adjacent to curb */
                                col = 5;
                            }
                            else
                            {
                                col = grid_col;
                            }

                            row_bits = (row_bits << 4) | col;
                        }
                        tile_dst[py] = row_bits;
                    }
                }
            }
        }
    }

    /* 5. Render Huge Roadside Skyscrapers & Distant City Blocks
     * Uses hyperbolic perspective mapping (i = 22400 / dz - 12) for perfectly smooth,
     * non-choppy 60 FPS motion matching the road scanlines down to the sub-pixel level.
     * Renders both foreground towering skyscrapers and background high-rises so both
     * sides of the road are densely packed with towering cyberpunk architecture.
     */
    static const uint16_t s_obj_world_z[8] = { 0, 256, 512, 768, 1024, 1280, 1536, 1792 };
    static const uint8_t  s_bldg_type[8]   = { 2, 3, 4, 3, 2, 4, 3, 2 };

    uint16_t world_pos_16 = (uint16_t)(road_pos_z << 3);
    int16_t order[8];
    uint16_t dists[8];

    for (int k = 0; k < 8; k++)
    {
        /* Distance ahead of camera in 0..2047 world units */
        dists[k] = (uint16_t)((s_obj_world_z[k] - world_pos_16) & 0x07FF);
        order[k] = (int16_t)k;
    }

    /* Sort buildings from farthest (highest distance) to closest (foreground) for painter's algorithm */
    for (int a = 0; a < 7; a++)
    {
        for (int b = a + 1; b < 8; b++)
        {
            if (dists[order[b]] > dists[order[a]])
            {
                int16_t tmp = order[a];
                order[a] = order[b];
                order[b] = tmp;
            }
        }
    }

    for (int idx = 0; idx < 8; idx++)
    {
        int k = order[idx];
        uint16_t dz = dists[k];

        /* Render buildings within visible hyperbolic depth range (170..1800) */
        if (dz >= 170 && dz <= 1800)
        {
            /* Perfectly smooth hyperbolic perspective scanline index i matching s_persp_z */
            int16_t i = (int16_t)((22400U / dz) - 12);
            if (i < 0)
            {
                i = 0;
            }
            if (i > 108)
            {
                i = 108;
            }

            int16_t cx = row_cx[i];
            int16_t shoulder = row_half_w[i] + row_curb_w[i];
            uint8_t type = s_bldg_type[k];

            /* Huge skyscraper dimensions: 14..58px wide, 35..110px tall */
            int16_t b_w = (int16_t)(14 + ((i * 32) >> 6));
            int16_t b_h = (type == 2) ? (int16_t)((b_w * 11) >> 2) /* Spire: up to 110px tall */
                        : (type == 3) ? (int16_t)((b_w * 9) >> 2)  /* Megacorp: up to 100px tall */
                        : (int16_t)((b_w * 5) >> 1);               /* Arcology: up to 90px tall */
            if (b_h > 110)
            {
                b_h = 110;
            }

            /* --- Left Side: Background Skyscraper (further out) --- */
            int16_t bg_w = (b_w * 5) >> 2;
            int16_t bg_h = (b_h * 5) >> 2;
            int16_t l_bg_cx = cx - shoulder - (b_w) - (bg_w >> 1);
            int16_t l_bg_tx = l_bg_cx >> 3;
            int16_t l_bg_bank_dy = (int16_t)(((l_bg_tx - 10) * bank_slope) >> 3);
            int16_t l_bg_base_y = (int16_t)(HORIZON_Y + i - l_bg_bank_dy);
            render_scaled_rotated_roadside_obj(wram_tiles, l_bg_cx, l_bg_base_y, bg_w, bg_h, cos_bank, sin_bank, (type == 2) ? 3 : 2);

            /* --- Right Side: Background Skyscraper (further out) --- */
            int16_t r_bg_cx = cx + shoulder + (b_w) + (bg_w >> 1);
            int16_t r_bg_tx = r_bg_cx >> 3;
            int16_t r_bg_bank_dy = (int16_t)(((r_bg_tx - 10) * bank_slope) >> 3);
            int16_t r_bg_base_y = (int16_t)(HORIZON_Y + i - r_bg_bank_dy);
            render_scaled_rotated_roadside_obj(wram_tiles, r_bg_cx, r_bg_base_y, bg_w, bg_h, cos_bank, sin_bank, (type == 4) ? 2 : 4);

            /* --- Left Side: Huge Foreground Skyscraper (roadside) --- */
            int16_t left_cx = cx - shoulder - 2 - (b_w >> 1);
            int16_t left_tx = left_cx >> 3;
            int16_t left_bank_dy = (int16_t)(((left_tx - 10) * bank_slope) >> 3);
            int16_t left_base_y = (int16_t)(HORIZON_Y + i - left_bank_dy);
            render_scaled_rotated_roadside_obj(wram_tiles, left_cx, left_base_y, b_w, b_h, cos_bank, sin_bank, type);

            /* --- Right Side: Huge Foreground Skyscraper (roadside) --- */
            int16_t right_cx = cx + shoulder + 2 + (b_w >> 1);
            int16_t right_tx = right_cx >> 3;
            int16_t right_bank_dy = (int16_t)(((right_tx - 10) * bank_slope) >> 3);
            int16_t right_base_y = (int16_t)(HORIZON_Y + i - right_bank_dy);
            render_scaled_rotated_roadside_obj(wram_tiles, right_cx, right_base_y, b_w, b_h, cos_bank, sin_bank, (type == 3) ? 4 : 3);
        }
    }
}
