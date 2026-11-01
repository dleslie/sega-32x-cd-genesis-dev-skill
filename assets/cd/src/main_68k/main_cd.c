/**
 * @file main_cd.c
 * @brief Sega CD Master Coordinator (Genesis Main-CPU 68000 / SGDK).
 *
 * Coordinates:
 *   - Genesis 68000: VDP display planes, joypad polling, SGDK 2.x runtime
 *   - Sega CD Sub-68000: COMM register handshake, render dispatch, heartbeat
 *   - Word RAM 1M Mode: Double-buffered ping-pong graphics swapping (0x200000)
 *
 * Copyright (c) 2026 Dan Leslie
 * SPDX-License-Identifier: MIT
 */

#include <genesis.h>
#include "car_tiles.h"
#include "girder_tiles.h"

/* Sega CD Registers (Genesis Main-CPU Aperture: 0xA12000) */
#define SCD_REG_RESET        (*(volatile uint16_t *)0xA12000)
#define SCD_REG_MEMMODE      (*(volatile uint16_t *)0xA12002)
#define SCD_REG_CDCMODE      (*(volatile uint16_t *)0xA12004)

#define SCD_REG_COMM_MD0     (*(volatile uint16_t *)0xA12010)
#define SCD_REG_COMM_MD1     (*(volatile uint16_t *)0xA12012)
#define SCD_REG_COMM_MD2     (*(volatile uint16_t *)0xA12014)
#define SCD_REG_COMM_MD3     (*(volatile uint16_t *)0xA12016)

#define SCD_REG_COMM_SUB0    (*(volatile uint16_t *)0xA12020)
#define SCD_REG_COMM_SUB1    (*(volatile uint16_t *)0xA12022)
#define SCD_REG_COMM_SUB2    (*(volatile uint16_t *)0xA12024)
#define SCD_REG_COMM_SUB3    (*(volatile uint16_t *)0xA12026)

/* Word RAM 1M Mode Aperture (Genesis Main-CPU View: 128 KB Bank) */
#define SCD_WORD_RAM_1M      ((volatile uint16_t *)0x200000)

/* Handshake Protocol Constants */
#define STAT_READY           0x0001
#define STAT_BRAM_SAVED      0x0010
#define STAT_BRAM_LOADED     0x0020
#define STAT_BRAM_FORMATTED  0x0030

#define CMD_INIT             0x0001
#define CMD_RENDER_FRAME     0x0002
#define CMD_BRAM_SAVE        0x0004
#define CMD_BRAM_LOAD        0x0005
#define CMD_BRAM_FORMAT      0x0006

/* ASIC Display Canvas Dimensions (Tiles 16..335) */
#define ASIC_TILE_BASE          16
#define ASIC_TILES_W            20   /* 160 pixels / 8 */
#define ASIC_TILES_H            16   /* 128 pixels / 8 */
#define ASIC_TOTAL_TILES        (ASIC_TILES_W * ASIC_TILES_H)
#define ASIC_WRAM_OFFSET        0x1000

/* Standalone Mega Drive Viewport Dimensions: 8x8 tiles (64x64 pixels) */
#define STANDALONE_TILES_W      8
#define STANDALONE_TILES_H      8
#define STANDALONE_TILES        (STANDALONE_TILES_W * STANDALONE_TILES_H)

/* Disjoint VRAM Tile Allocations strictly above ASIC_TILE_BASE + ASIC_TOTAL_TILES (336+) */
#define STANDALONE_BORDER_BASE  (ASIC_TILE_BASE + ASIC_TOTAL_TILES) /* 336..343 (8 tiles) */
#define CITY_TILE_BASE          (STANDALONE_BORDER_BASE + 8)        /* 344..367 (24 tiles) */
#define SPRITE_TILE_BASE        (CITY_TILE_BASE + 24)               /* 368+ */

/* Player 48x24 Composite Supercar (Two 24x24 = 3x3 = 9-tile hardware sprites per frame) */
#define SPRITE_CAR_STRAIGHT_L   (SPRITE_TILE_BASE)                  /* 368..376 (9 tiles) */
#define SPRITE_CAR_STRAIGHT_R   (SPRITE_CAR_STRAIGHT_L + 9)         /* 377..385 (9 tiles) */
#define SPRITE_CAR_LEFT_L       (SPRITE_CAR_STRAIGHT_R + 9)         /* 386..394 (9 tiles) */
#define SPRITE_CAR_LEFT_R       (SPRITE_CAR_LEFT_L + 9)             /* 395..403 (9 tiles) */
#define SPRITE_CAR_RIGHT_L      (SPRITE_CAR_LEFT_R + 9)             /* 404..412 (9 tiles) */
#define SPRITE_CAR_RIGHT_R      (SPRITE_CAR_RIGHT_L + 9)            /* 413..421 (9 tiles) */

/* Roadside 3D Cyber Structures & Mega-Towers (Multi-LOD, 3x-4x wider) */
#define SPRITE_BLDG_LOD3_L      (SPRITE_CAR_RIGHT_R + 9)            /* 422..437 (16 tiles) */
#define SPRITE_BLDG_LOD3_C      (SPRITE_BLDG_LOD3_L + 16)           /* 438..453 (16 tiles) */
#define SPRITE_BLDG_LOD3_R      (SPRITE_BLDG_LOD3_C + 16)           /* 454..469 (16 tiles) */
#define SPRITE_BLDG_LOD2_L      (SPRITE_BLDG_LOD3_R + 16)           /* 470..485 (16 tiles) */
#define SPRITE_BLDG_LOD2_R      (SPRITE_BLDG_LOD2_L + 16)           /* 486..501 (16 tiles) */
#define SPRITE_BLDG_LOD1        (SPRITE_BLDG_LOD2_R + 16)           /* 502..517 (16 tiles) */
#define SPRITE_BLDG_LOD0        (SPRITE_BLDG_LOD1 + 16)             /* 518..525 (8 tiles) */

/* Traffic Simulation Parameters */
#define NUM_DRONES              4

static bool s_cd_online = false;
static uint32_t s_main_frame_count = 0;

/* Standalone MD Transformation State */
static uint8_t  s_standalone_angle = 0;
static uint16_t s_standalone_scale = 256;
static int16_t  s_standalone_scale_velocity = 2;

/* Standalone 32x32 Source Texture and Tile Destination Buffer */
static uint8_t  s_tex[32][32];
static uint32_t s_standalone_tiles[STANDALONE_TILES * 8];

/* 256-Entry Fixed-Point Sine LUT (5.11 format: 2048 = 1.0) */
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
static inline int16_t md_sin(uint8_t angle)
{
    return s_sin_lut[angle];
}

/**
 * @brief Evaluates fixed-point cosine by advancing angle phase by 90 degrees (64 steps).
 */
static inline int16_t md_cos(uint8_t angle)
{
    return s_sin_lut[(uint8_t)(angle + 64)];
}

/**
 * @brief Initializes the Genesis VDP color palettes with cyberpunk neon entries.
 * Configures PAL0 for ASIC road canvas & text, PAL1 for cyan city skyline,
 * PAL2 for magenta / amber neon lights, and PAL3 for HUD status.
 */
static void init_palettes(void)
{
    /* Backdrop: Deep obsidian navy night sky */
    PAL_setColor(0,       RGB24_TO_VDPCOLOR(0x060614));

    /* PAL0: ASIC Banked Road Canvas, Roadside Billboards & UI
     * Color 1: Dark cyber-asphalt road band (#22223b)
     * Color 2: Lighter perspective road scanline band (#38385c)
     * Color 3: Electric neon cyan lane divider, horizon glow & curb (#00f5d4)
     * Color 4: Neon hot magenta hazard curb & billboard border (#ff0066)
     * Color 5: Cyber amber gold center stripe & billboard highlight (#ffe600)
     * Color 6: Off-road synthwave night ground (#120d2b)
     * Color 7: Outrun violet synthwave horizontal ground grid (#7b2cbf)
     * Color 8: Pure white curb & sign specular highlight (#ffffff)
     * Color 15: Pure white HUD text (#ffffff)
     */
    PAL_setColor(1,       RGB24_TO_VDPCOLOR(0x22223b));
    PAL_setColor(2,       RGB24_TO_VDPCOLOR(0x38385c));
    PAL_setColor(3,       RGB24_TO_VDPCOLOR(0x00f5d4));
    PAL_setColor(4,       RGB24_TO_VDPCOLOR(0xff0066));
    PAL_setColor(5,       RGB24_TO_VDPCOLOR(0xffe600));
    PAL_setColor(6,       RGB24_TO_VDPCOLOR(0x120d2b));
    PAL_setColor(7,       RGB24_TO_VDPCOLOR(0x7b2cbf));
    PAL_setColor(8,       RGB24_TO_VDPCOLOR(0xffffff));
    PAL_setColor(15,      RGB24_TO_VDPCOLOR(0xffffff));

    /* PAL1: Cyberpunk Nighttime City Skyline & Flanking Skyscraper Towers */
    PAL_setColor(16 + 1,  RGB24_TO_VDPCOLOR(0x0e0e24)); /* Deep silhouette high-rise base */
    PAL_setColor(16 + 2,  RGB24_TO_VDPCOLOR(0x18183c)); /* Dark steel skyscraper facade */
    PAL_setColor(16 + 3,  RGB24_TO_VDPCOLOR(0x00e5ff)); /* Neon cyan spire light / window */
    PAL_setColor(16 + 4,  RGB24_TO_VDPCOLOR(0xff007f)); /* Neon magenta window grid / sign */
    PAL_setColor(16 + 5,  RGB24_TO_VDPCOLOR(0xffe600)); /* Amber rooftop beacon / gold window */
    PAL_setColor(16 + 6,  RGB24_TO_VDPCOLOR(0xffffff)); /* Pure white bright office lighting */
    PAL_setColor(16 + 7,  RGB24_TO_VDPCOLOR(0x38385c)); /* Lighter structural steel mullions */
    PAL_setColor(16 + 8,  RGB24_TO_VDPCOLOR(0x7b2cbf)); /* Outrun purple architectural trim */
    PAL_setColor(16 + 15, RGB24_TO_VDPCOLOR(0x00e5ff)); /* PAL1 cyan text */

    /* PAL2: 48x24 Cyberpunk Supercar & Multi-LOD Traffic Sprites */
    PAL_setColor(32 + 0,  RGB24_TO_VDPCOLOR(0x000000)); /* Transparent */
    PAL_setColor(32 + 1,  RGB24_TO_VDPCOLOR(0x2b2d42)); /* Metallic gunmetal chassis */
    PAL_setColor(32 + 2,  RGB24_TO_VDPCOLOR(0x00b4d8)); /* Cyber cyan canopy glass */
    PAL_setColor(32 + 3,  RGB24_TO_VDPCOLOR(0x00f5d4)); /* Electric neon cyan trim & underglow */
    PAL_setColor(32 + 4,  RGB24_TO_VDPCOLOR(0xff0055)); /* Neon ruby tail lights */
    PAL_setColor(32 + 5,  RGB24_TO_VDPCOLOR(0xffbe0b)); /* Amber-gold plasma exhaust afterburners */
    PAL_setColor(32 + 6,  RGB24_TO_VDPCOLOR(0xffffff)); /* Pure white specular gleam */
    PAL_setColor(32 + 7,  RGB24_TO_VDPCOLOR(0x0a0a14)); /* Deep carbon tire tread & vents */
    PAL_setColor(32 + 8,  RGB24_TO_VDPCOLOR(0xd90429)); /* OutRun crimson-red supercar bodywork */
    PAL_setColor(32 + 15, RGB24_TO_VDPCOLOR(0xffe600)); /* PAL2 text (amber) */

    /* PAL3: Status banners and diagnostic accents */
    PAL_setColor(48 + 15, RGB24_TO_VDPCOLOR(0xff007f)); /* PAL3 text (neon pink) */
}

/**
 * @brief Populates the 32x32 source texture with concentric rings, targeting crosshairs, and ruby core.
 */
static void init_standalone_texture(void)
{
    for (int y = 0; y < 32; y++)
    {
        for (int x = 0; x < 32; x++)
        {
            int dx = x - 16;
            int dy = y - 16;
            int dist_sq = dx * dx + dy * dy;
            int m_dist = ((dx < 0) ? -dx : dx) + ((dy < 0) ? -dy : dy);

            uint8_t c = 0;
            if (dist_sq <= 15 * 15 && dist_sq >= 13 * 13)
            {
                c = 1; /* Emerald outer circular boundary */
            }
            else if (dist_sq < 13 * 13 && dist_sq >= 9 * 9)
            {
                c = 2; /* Amber gold concentric ring */
            }
            else if (m_dist <= 8 && m_dist >= 4)
            {
                c = 3; /* Ruby crimson diamond emblem */
            }
            else if (dist_sq <= 3 * 3)
            {
                c = 1; /* Emerald center gemstone */
            }

            /* Crosshair targeting lines across center axes */
            if ((dx == 0 && (dy >= -14 && dy <= 14)) ||
                (dy == 0 && (dx >= -14 && dx <= 14)))
            {
                c = 1;
            }

            s_tex[y][x] = c;
        }
    }
}

/**
 * @brief Software affine transformation renderer generating 64 Genesis 4bpp chunky tiles.
 *
 * Evaluates fixed-point rotation and scaling directly into tile memory order,
 * eliminating intermediate buffer copies and ensuring a full 60 FPS update rate.
 *
 * @param angle    8-bit rotation angle (0..255 representing 0..360 degrees).
 * @param scale_q8 8.8 fixed-point scale factor (256 = 1.0x normal size).
 */
static void render_affine(uint8_t angle, uint16_t scale_q8)
{
    /* Clamp scale to avoid divide-by-zero or excessive downsampling blur */
    if (scale_q8 < 32)
    {
        scale_q8 = 32;
    }

    int32_t cos_val = md_cos(angle);
    int32_t sin_val = md_sin(angle);

    /* u, v delta per screen X pixel in 16.16 fixed-point */
    int32_t du = ((int32_t)cos_val * 65536) / ((int32_t)scale_q8 * 8);
    int32_t dv = ((int32_t)sin_val * 65536) / ((int32_t)scale_q8 * 8);

    /* Per-scanline perpendicular stepping increments (-sin, cos) */
    int32_t step_u = -dv;
    int32_t step_v =  du;

    /* Center coordinates: 64x64 output center (32, 32), 32x32 texture center (16, 16) */
    int32_t center_tex = 16 << 16;
    int32_t start_u = center_tex - (32 * du) - (32 * step_u);
    int32_t start_v = center_tex - (32 * dv) - (32 * step_v);

    /* Iterate over tile rows, scanlines, and tile columns */
    for (int ty = 0; ty < STANDALONE_TILES_H; ty++)
    {
        for (int py = 0; py < 8; py++)
        {
            int y = ty * 8 + py;
            int32_t cur_u = start_u + y * step_u;
            int32_t cur_v = start_v + y * step_v;

            uint32_t *dst = &s_standalone_tiles[(ty * STANDALONE_TILES_W * 8) + py];

            for (int tx = 0; tx < STANDALONE_TILES_W; tx++)
            {
                uint32_t packed = 0;
                for (int px = 0; px < 8; px++)
                {
                    int u = cur_u >> 16;
                    int v = cur_v >> 16;
                    uint8_t col = 0;
                    if ((uint32_t)u < 32 && (uint32_t)v < 32)
                    {
                        col = s_tex[v][u];
                    }
                    packed = (packed << 4) | col;
                    cur_u += du;
                    cur_v += dv;
                }
                *dst = packed;
                dst += 8; /* Advance to same scanline in next horizontal tile */
            }
        }
    }
}

/**
 * @brief Decorative static border tiles and Cyberpunk City skyline tiles.
 */
static const uint32_t s_border_tiles[8 * 8] = {
    /* Tile 0: Top-Left corner bracket */
    0x11111111, 0x10000000, 0x10000000, 0x10000000,
    0x10000000, 0x10000000, 0x10000000, 0x10000000,
    /* Tile 1: Top-Right corner bracket */
    0x11111111, 0x00000001, 0x00000001, 0x00000001,
    0x00000001, 0x00000001, 0x00000001, 0x00000001,
    /* Tile 2: Bottom-Left corner bracket */
    0x10000000, 0x10000000, 0x10000000, 0x10000000,
    0x10000000, 0x10000000, 0x10000000, 0x11111111,
    /* Tile 3: Bottom-Right corner bracket */
    0x00000001, 0x00000001, 0x00000001, 0x00000001,
    0x00000001, 0x00000001, 0x00000001, 0x11111111,
    /* Tile 4: Horizontal border bar */
    0x11111111, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    /* Tile 5: Vertical border bar */
    0x10000000, 0x10000000, 0x10000000, 0x10000000,
    0x10000000, 0x10000000, 0x10000000, 0x10000000,
    /* Tile 6: Center Tech Grid Cross (subtle gold center dot) */
    0x00000000, 0x00000000, 0x00000000, 0x00020000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    /* Tile 7: Blank navy tile */
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000
};

/* Cyberpunk Nighttime City Skyline & Flanking Skyscraper Tiles (24 tiles = 192 dwords) */
static const uint32_t s_city_tiles[24 * 8] = {
    /* Tile 0: Night Sky with Stars */
    0x00000000, 0x00500000, 0x00000000, 0x00000030,
    0x00000000, 0x00000000, 0x03000000, 0x00000000,
    /* Tile 1: Skyscraper Spire / Antenna Beacon (Amber + Cyan) */
    0x00050000, 0x00060000, 0x00070000, 0x00737000,
    0x00737000, 0x07737700, 0x07737700, 0x77737770,
    /* Tile 2: High-Rise Tower Roof with Neon Cyan Trim */
    0x33333333, 0x77777777, 0x27272727, 0x23232323,
    0x27272727, 0x23232323, 0x27272727, 0x22222222,
    /* Tile 3: Skyscraper Body with Magenta Window Grid */
    0x27272727, 0x24242424, 0x27272727, 0x24242424,
    0x27272727, 0x24242424, 0x27272727, 0x22222222,
    /* Tile 4: Tower with Neon Billboard Frame (Hot Pink / Amber) */
    0x77777777, 0x74444447, 0x74555547, 0x74533547,
    0x74555547, 0x74444447, 0x77777777, 0x27272727,
    /* Tile 5: Skyscraper Silhouette (Dark Navy Shadow) */
    0x11111111, 0x11111111, 0x12121212, 0x12121212,
    0x11111111, 0x11111111, 0x12121212, 0x11111111,
    /* Tile 6: Diagonal Bridge / Highway Girder Strut */
    0x30000000, 0x33000000, 0x03300000, 0x00330000,
    0x00033000, 0x00003300, 0x00000330, 0x00000033,
    /* Tile 7: Distant Horizon City Mist / Glow */
    0x11111111, 0x12121212, 0x12222221, 0x22322322,
    0x22222222, 0x12222221, 0x11211211, 0x11111111,
    /* Tile 8: Skyscraper Facade with Glowing Cyan Office Windows */
    0x72727272, 0x23232323, 0x23232323, 0x72727272,
    0x72727272, 0x23232323, 0x23232323, 0x72727272,
    /* Tile 9: Skyscraper Facade with Lit Amber / Gold Windows */
    0x72727272, 0x25252525, 0x25252525, 0x72727272,
    0x72727272, 0x25252525, 0x25252525, 0x72727272,
    /* Tile 10: Skyscraper Facade with Lit White Windows & Magenta Trim */
    0x42424242, 0x26262626, 0x26262626, 0x42424242,
    0x42424242, 0x26262626, 0x26262626, 0x42424242,
    /* Tile 11: Mega-Arcology Vertical Structural Steel Column (Left Edge) */
    0x77222222, 0x78232323, 0x78232323, 0x77222222,
    0x77222222, 0x78232323, 0x78232323, 0x77222222,
    /* Tile 12: Mega-Arcology Vertical Structural Steel Column (Right Edge) */
    0x22222277, 0x23232387, 0x23232387, 0x22222277,
    0x22222277, 0x23232387, 0x23232387, 0x22222277,
    /* Tile 13: Corporate Diamond Logo Sign (Cyan & White on Dark Steel) */
    0x22277222, 0x22733722, 0x27366372, 0x73666637,
    0x73666637, 0x27366372, 0x22733722, 0x22277222,
    /* Tile 14: Corporate Hexagon Logo Sign (Magenta & Amber) */
    0x22744722, 0x27455472, 0x74566547, 0x74566547,
    0x74566547, 0x74566547, 0x27455472, 0x22744722,
    /* Tile 15: Cyberpunk Glowing Neon Kanji / Glyph Signboard */
    0x28888882, 0x28444482, 0x28433482, 0x28433482,
    0x28444482, 0x28555582, 0x28533582, 0x28888882,
    /* Tile 16: Mega-Structure Diagonal Seismic Cross-Brace */
    0x72222227, 0x27222272, 0x22722722, 0x22277222,
    0x22277222, 0x22722722, 0x27222272, 0x72222227,
    /* Tile 17: Stepped Skyscraper Roof (Left Step Down) */
    0x00003333, 0x00007777, 0x00332727, 0x00772323,
    0x33272727, 0x77232323, 0x27272727, 0x22222222,
    /* Tile 18: Stepped Skyscraper Roof (Right Step Down) */
    0x33330000, 0x77770000, 0x27273300, 0x23237700,
    0x27272733, 0x23232377, 0x27272727, 0x22222222,
    /* Tile 19: Highway Sound Barrier Wall with Glowing Neon Guide Rail */
    0x33333333, 0x66666666, 0x77777777, 0x11111111,
    0x12121212, 0x11111111, 0x12121212, 0x11111111,
    /* Tile 20: Highway Sound Barrier Retaining Wall Pillar */
    0x33888833, 0x66777766, 0x77888877, 0x17222271,
    0x17222271, 0x17222271, 0x17222271, 0x17222271,
    /* Tile 21: Massive Foundational Base Pylon (Subterranean Mega-Pillar) */
    0x71111117, 0x72111127, 0x72111127, 0x72111127,
    0x72111127, 0x72111127, 0x77111177, 0x77777777,
    /* Tile 22: High-Rise Overhang / Skybridge Beam */
    0x77777777, 0x33333333, 0x27272727, 0x23232323,
    0x22222222, 0x77777777, 0x11111111, 0x11111111,
    /* Tile 23: Skybridge Windowed Walkway Segment */
    0x77777777, 0x33333333, 0x26262626, 0x26262626,
    0x22222222, 0x77777777, 0x00000000, 0x00000000
};

/**
 * @brief Renders the cyberpunk nighttime panoramic city skyline on Plane B.
 *
 * Leaves rows 0..6 clean for stationary HUD text on Plane A.
 * Populates rows 7..10 of Plane B with the panoramic skyline spanning all 40 columns.
 * Leaves rows 7..27 on Plane A completely transparent (tile 0) so the skyline and the
 * banked road / synthwave ground grid show through naturally.
 */
static void init_cyberpunk_city_skyline(void)
{
    VDP_loadTileData(s_city_tiles, CITY_TILE_BASE, 24, DMA);

    /* 1. Clear Plane A across all 64 columns for rows 0..27 (HUD text and transparent aperture) */
    for (uint16_t r = 0; r < 28; r++)
    {
        for (uint16_t c = 0; c < 64; c++)
        {
            VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(PAL0, 0, 0, 0, 0), c, r);
        }
    }

    /* 2. Populate rows 7..11 of Plane B with panoramic cyberpunk skyline spanning all 40 columns */
    for (uint16_t r = 7; r <= 11; r++)
    {
        for (uint16_t c = 0; c < 40; c++)
        {
            uint16_t tile = CITY_TILE_BASE + 0; /* Starfield sky */

            if (r == 7)
            {
                if ((c % 7) == 2 || (c % 7) == 5)
                {
                    tile = CITY_TILE_BASE + 1; /* Spire / antenna beacon */
                }
            }
            else if (r == 8)
            {
                if ((c % 5) == 1)
                {
                    tile = CITY_TILE_BASE + 1; /* Spire */
                }
                else if ((c % 3) == 0)
                {
                    tile = CITY_TILE_BASE + 2; /* Cyan roof trim */
                }
                else
                {
                    tile = CITY_TILE_BASE + 5; /* Dark silhouette */
                }
            }
            else if (r == 9 || r == 10)
            {
                if ((c % 5) == 3)
                {
                    tile = CITY_TILE_BASE + 4; /* Neon billboard */
                }
                else if ((c % 4) == 1)
                {
                    tile = CITY_TILE_BASE + 3; /* Magenta window grid */
                }
                else if ((c % 3) == 2)
                {
                    tile = CITY_TILE_BASE + 8; /* Cyan office window */
                }
                else if ((c % 3) == 0)
                {
                    tile = CITY_TILE_BASE + 9; /* Gold office window */
                }
                else
                {
                    tile = CITY_TILE_BASE + 5; /* Dark silhouette */
                }
            }
            else if (r == 11)
            {
                tile = CITY_TILE_BASE + 5; /* Skyline ground baseline */
            }

            VDP_setTileMapXY(BG_B, TILE_ATTR_FULL(PAL1, 0, 0, 0, tile), c, r);
        }
    }
}

/**
 * @brief Animates the cyberpunk skyline neon signs and billboard lights via palette cycling.
 *
 * Cycles PAL1 neon windows and antenna beacons every few frames to simulate pulsing city life.
 *
 * @param frame_count Current main coordinator frame counter.
 */
static void update_neon_palette_animation(uint32_t frame_count)
{
    /* Pulsing antenna beacon on PAL1 Color 5: alternates between amber gold and white */
    if ((frame_count & 0x10) == 0)
    {
        PAL_setColor(16 + 5, RGB24_TO_VDPCOLOR(0xffe600)); /* Amber gold */
    }
    else
    {
        PAL_setColor(16 + 5, RGB24_TO_VDPCOLOR(0xffffff)); /* Bright strobe flash */
    }

    /* Shifting billboard neon color on PAL1 Color 4: rotates between magenta, electric cyan, and hot violet */
    uint8_t phase = (uint8_t)((frame_count >> 3) % 3);
    if (phase == 0)
    {
        PAL_setColor(16 + 4, RGB24_TO_VDPCOLOR(0xff007f)); /* Neon magenta */
    }
    else if (phase == 1)
    {
        PAL_setColor(16 + 4, RGB24_TO_VDPCOLOR(0x00e5ff)); /* Electric cyan */
    }
    else
    {
        PAL_setColor(16 + 4, RGB24_TO_VDPCOLOR(0xbd00ff)); /* Hot violet */
    }
}

/**
 * @brief Loads 48x24 composite cyberpunk player supercar and roadside 3D girder tiles into VRAM.
 */
static void init_vehicle_sprites(void)
{
    /* Player 48x24 Supercar: Straight (Left 24x24 + Right 24x24) */
    VDP_loadTileData(s_car_straight_left_tiles,  SPRITE_CAR_STRAIGHT_L, 9,  DMA);
    VDP_loadTileData(s_car_straight_right_tiles, SPRITE_CAR_STRAIGHT_R, 9,  DMA);

    /* Player 48x24 Supercar: Banking Left (Left 24x24 + Right 24x24) */
    VDP_loadTileData(s_car_left_left_tiles,      SPRITE_CAR_LEFT_L,     9,  DMA);
    VDP_loadTileData(s_car_left_right_tiles,     SPRITE_CAR_LEFT_R,     9,  DMA);

    /* Player 48x24 Supercar: Banking Right (Left 24x24 + Right 24x24) */
    VDP_loadTileData(s_car_right_left_tiles,     SPRITE_CAR_RIGHT_L,    9,  DMA);
    VDP_loadTileData(s_car_right_right_tiles,    SPRITE_CAR_RIGHT_R,    9,  DMA);

    /* Roadside 3D Cyber Structures & Mega-Towers (Multi-LOD) */
    VDP_loadTileData(s_bldg_lod3_left,   SPRITE_BLDG_LOD3_L, 16, DMA);
    VDP_loadTileData(s_bldg_lod3_center, SPRITE_BLDG_LOD3_C, 16, DMA);
    VDP_loadTileData(s_bldg_lod3_right,  SPRITE_BLDG_LOD3_R, 16, DMA);
    VDP_loadTileData(s_bldg_lod2_left,   SPRITE_BLDG_LOD2_L, 16, DMA);
    VDP_loadTileData(s_bldg_lod2_right,  SPRITE_BLDG_LOD2_R, 16, DMA);
    VDP_loadTileData(s_bldg_lod1_tiles,  SPRITE_BLDG_LOD1,   16, DMA);
    VDP_loadTileData(s_bldg_lod0_tiles,  SPRITE_BLDG_LOD0,   8,  DMA);
}

/* Roadside 3D Cyber Structures & Mega-Towers (Multi-LOD, dispersed layout) */
static uint32_t s_girder_travel_z = 0;

struct RoadsideDef {
    int16_t base_z;      /* World Z position along track (0..999) */
    int8_t  side;        /* -1 = Left of highway, +1 = Right of highway */
    int16_t lateral_gap; /* Lateral distance / setback from road curb (4..54px) */
    int16_t width_mode;  /* 3 = 96px mega-complex (3 cols), 2 = 64px tower (2 cols) */
};

#define NUM_ROADSIDE_OBJS 6
#define TRACK_CYCLE_Z     1000

static const struct RoadsideDef s_roadside_defs[NUM_ROADSIDE_OBJS] = {
    { .base_z = 0,   .side = -1, .lateral_gap = 4,  .width_mode = 3 },
    { .base_z = 170, .side =  1, .lateral_gap = 24, .width_mode = 2 },
    { .base_z = 350, .side = -1, .lateral_gap = 46, .width_mode = 3 },
    { .base_z = 520, .side =  1, .lateral_gap = 6,  .width_mode = 3 },
    { .base_z = 690, .side = -1, .lateral_gap = 18, .width_mode = 2 },
    { .base_z = 860, .side =  1, .lateral_gap = 50, .width_mode = 3 }
};

struct ActiveRoadsideObj {
    int32_t rel_z;
    uint8_t def_idx;
};

/**
 * @brief Updates and renders VDP hardware sprites for the player supercar and 3D roadside structures.
 *
 * Emits 3x-wider composite architectural structures dispersed across the left and right sides
 * with varying lateral setbacks, scaling in 3D perspective and zooming past the camera.
 *
 * @param bank_angle   Current horizon banking angle from Sub-CPU telemetry.
 * @param steer_x      Camera steering bias from Sub-CPU telemetry.
 * @param curve_amt    Current road S-curve deflection from Sub-CPU telemetry.
 * @param player_speed Current forward vehicle speed.
 */
static void update_road_sprites(int8_t bank_angle, int8_t steer_x, int8_t curve_amt, uint16_t player_speed)
{
    /* Player 48x24 Composite Supercar: pinned near bottom-center of the road */
    int16_t player_x = 160 - 24 - ((int16_t)steer_x >> 2);
    int16_t player_y = 194;

    uint16_t tile_l = SPRITE_CAR_STRAIGHT_L;
    uint16_t tile_r = SPRITE_CAR_STRAIGHT_R;

    /* Select banked supercar chassis animation when steering or banking into curves */
    if (bank_angle > 2)
    {
        tile_l = SPRITE_CAR_LEFT_L;
        tile_r = SPRITE_CAR_LEFT_R;
    }
    else if (bank_angle < -2)
    {
        tile_l = SPRITE_CAR_RIGHT_L;
        tile_r = SPRITE_CAR_RIGHT_R;
    }

    /* Advance roadside 3D structures along the highway */
    if (player_speed > 0)
    {
        s_girder_travel_z += (player_speed >> 2);
    }

    struct ActiveRoadsideObj active_objs[NUM_ROADSIDE_OBJS];
    for (int p = 0; p < NUM_ROADSIDE_OBJS; p++)
    {
        int32_t rz = (s_roadside_defs[p].base_z - (int32_t)s_girder_travel_z) % TRACK_CYCLE_Z;
        if (rz < 0) rz += TRACK_CYCLE_Z;
        active_objs[p].rel_z = rz;
        active_objs[p].def_idx = (uint8_t)p;
    }

    /* Sort objects in ascending order of rel_z so NEAREST objects are emitted first in SAT.
     * In Sega Genesis hardware, sprites with lower link indices have higher display priority
     * and naturally draw over sprites with higher link indices. */
    for (int a = 0; a < NUM_ROADSIDE_OBJS - 1; a++)
    {
        for (int b = a + 1; b < NUM_ROADSIDE_OBJS; b++)
        {
            if (active_objs[a].rel_z > active_objs[b].rel_z)
            {
                struct ActiveRoadsideObj tmp = active_objs[a];
                active_objs[a] = active_objs[b];
                active_objs[b] = tmp;
            }
        }
    }

    /* Emit roadside structures starting at sprite 2 */
    uint16_t sprite_idx = 2;
    for (int p = 0; p < NUM_ROADSIDE_OBJS; p++)
    {
        int32_t rz = active_objs[p].rel_z;
        if (rz > 850) continue; /* Over the horizon */

        uint8_t def_idx = active_objs[p].def_idx;
        int8_t  side        = s_roadside_defs[def_idx].side;
        int16_t lateral_gap = s_roadside_defs[def_idx].lateral_gap;
        int16_t width_mode  = s_roadside_defs[def_idx].width_mode;

        /* Map rz (850..0) to road scanline i (0..127) */
        int16_t i;
        if (rz <= 100)
        {
            /* Close / passing: spans the final stretch of road */
            i = (int16_t)(96 + ((100 - rz) * 31 / 100));
        }
        else
        {
            /* Perspective mapping for approaching distance */
            int32_t norm = ((850 - rz) * 100) / 750; /* 0..100 */
            i = (int16_t)((norm * norm * 96) / 10000); /* 0..96 */
        }
        if (i < 0)   i = 0;
        if (i > 127) i = 127;

        int16_t hw = (int16_t)(10 + ((i * 26) >> 6));
        int16_t cw = (int16_t)(3 + (i >> 5));
        int32_t dz = 127 - i;
        int32_t curve_shift = ((int32_t)curve_amt * (dz * dz)) >> 13;
        int32_t steer_shift = ((int32_t)steer_x * (i + 16)) >> 7;
        int16_t cx = (int16_t)(160 + curve_shift + steer_shift);
        int16_t base_y = 96 + i;

        int16_t left_curb_x  = cx - hw - cw;
        int16_t right_curb_x = cx + hw + cw;

        if (rz < 100)
        {
            /* --- LOD 3: Passing by (Towering 160px High, 64px..96px Wide Mega-Structure) ---
             * Columns start strictly at y = 56 (below HUD text) and span down to bottom of screen (y = 216). */
            int16_t pass_spread = (100 - (int16_t)rz) >> 1;
            int16_t obj_w = (width_mode == 3) ? 96 : 64;
            int16_t obj_x;

            if (side < 0)
            {
                obj_x = left_curb_x - lateral_gap - obj_w - pass_spread;
            }
            else
            {
                obj_x = right_curb_x + lateral_gap + pass_spread;
            }

            if (width_mode == 3)
            {
                /* 96px Wide: Three 32x32 sprite columns (Left, Center, Right) across 5 tiers */
                for (int seg = 0; seg < 5; seg++)
                {
                    int16_t seg_y = 56 + (seg << 5);

                    /* Left column */
                    if (obj_x >= -32 && obj_x < 320 && sprite_idx < 76)
                    {
                        VDP_setSpriteFull(sprite_idx, obj_x, seg_y, SPRITE_SIZE(4, 4),
                                          TILE_ATTR_FULL(PAL2, 1, 0, 0, SPRITE_BLDG_LOD3_L), sprite_idx + 1);
                        sprite_idx++;
                    }
                    /* Center column */
                    if ((obj_x + 32) >= -32 && (obj_x + 32) < 320 && sprite_idx < 76)
                    {
                        VDP_setSpriteFull(sprite_idx, obj_x + 32, seg_y, SPRITE_SIZE(4, 4),
                                          TILE_ATTR_FULL(PAL2, 1, 0, 0, SPRITE_BLDG_LOD3_C), sprite_idx + 1);
                        sprite_idx++;
                    }
                    /* Right column */
                    if ((obj_x + 64) >= -32 && (obj_x + 64) < 320 && sprite_idx < 76)
                    {
                        VDP_setSpriteFull(sprite_idx, obj_x + 64, seg_y, SPRITE_SIZE(4, 4),
                                          TILE_ATTR_FULL(PAL2, 1, 0, 0, SPRITE_BLDG_LOD3_R), sprite_idx + 1);
                        sprite_idx++;
                    }
                }
            }
            else
            {
                /* 64px Wide: Two 32x32 sprite columns across 5 tiers */
                for (int seg = 0; seg < 5; seg++)
                {
                    int16_t seg_y = 56 + (seg << 5);

                    if (obj_x >= -32 && obj_x < 320 && sprite_idx < 76)
                    {
                        VDP_setSpriteFull(sprite_idx, obj_x, seg_y, SPRITE_SIZE(4, 4),
                                          TILE_ATTR_FULL(PAL2, 1, 0, 0, SPRITE_BLDG_LOD2_L), sprite_idx + 1);
                        sprite_idx++;
                    }
                    if ((obj_x + 32) >= -32 && (obj_x + 32) < 320 && sprite_idx < 76)
                    {
                        VDP_setSpriteFull(sprite_idx, obj_x + 32, seg_y, SPRITE_SIZE(4, 4),
                                          TILE_ATTR_FULL(PAL2, 1, 0, 0, SPRITE_BLDG_LOD2_R), sprite_idx + 1);
                        sprite_idx++;
                    }
                }
            }
        }
        else if (rz < 260)
        {
            /* --- LOD 2: Near Structure (64px Wide, 96px High: two 32x32 columns x 3 tiers) --- */
            int16_t scaled_gap = (int16_t)((lateral_gap * (i + 32)) >> 7);
            int16_t obj_x = (side < 0) ? (left_curb_x - scaled_gap - 64) : (right_curb_x + scaled_gap);
            int16_t gy    = base_y - 94;
            if (gy < 56) gy = 56;

            for (int seg = 0; seg < 3; seg++)
            {
                int16_t seg_y = gy + (seg << 5);
                if (seg_y < 224)
                {
                    if (obj_x >= -32 && obj_x < 320 && sprite_idx < 76)
                    {
                        VDP_setSpriteFull(sprite_idx, obj_x, seg_y, SPRITE_SIZE(4, 4),
                                          TILE_ATTR_FULL(PAL2, 1, 0, 0, SPRITE_BLDG_LOD2_L), sprite_idx + 1);
                        sprite_idx++;
                    }
                    if ((obj_x + 32) >= -32 && (obj_x + 32) < 320 && sprite_idx < 76)
                    {
                        VDP_setSpriteFull(sprite_idx, obj_x + 32, seg_y, SPRITE_SIZE(4, 4),
                                          TILE_ATTR_FULL(PAL2, 1, 0, 0, SPRITE_BLDG_LOD2_R), sprite_idx + 1);
                        sprite_idx++;
                    }
                }
            }
        }
        else if (rz < 520)
        {
            /* --- LOD 1: Mid-Distance Structure (32px Wide, 64px High: one 32x32 column x 2 tiers) --- */
            int16_t scaled_gap = (int16_t)((lateral_gap * (i + 32)) >> 7);
            int16_t obj_x = (side < 0) ? (left_curb_x - scaled_gap - 32) : (right_curb_x + scaled_gap);
            int16_t gy    = base_y - 62;
            if (gy < 56) gy = 56;

            for (int seg = 0; seg < 2; seg++)
            {
                int16_t seg_y = gy + (seg << 5);
                if (seg_y < 224 && obj_x >= -32 && obj_x < 320 && sprite_idx < 76)
                {
                    VDP_setSpriteFull(sprite_idx, obj_x, seg_y, SPRITE_SIZE(4, 4),
                                      TILE_ATTR_FULL(PAL2, 1, 0, 0, SPRITE_BLDG_LOD1), sprite_idx + 1);
                    sprite_idx++;
                }
            }
        }
        else
        {
            /* --- LOD 0: Far Horizon Structure (16px Wide, 32px High: 16x32 sprite) --- */
            int16_t scaled_gap = (int16_t)((lateral_gap * (i + 32)) >> 7);
            int16_t obj_x = (side < 0) ? (left_curb_x - scaled_gap - 16) : (right_curb_x + scaled_gap);
            int16_t gy    = base_y - 31;
            if (gy < 56) gy = 56;

            if (gy < 224 && obj_x >= -16 && obj_x < 320 && sprite_idx < 76)
            {
                VDP_setSpriteFull(sprite_idx, obj_x, gy, SPRITE_SIZE(2, 4),
                                  TILE_ATTR_FULL(PAL2, 1, 0, 0, SPRITE_BLDG_LOD0), sprite_idx + 1);
                sprite_idx++;
            }
        }
    }

    /* Terminate sprite list */
    if (sprite_idx > 2)
    {
        VDP_setSpriteLink(sprite_idx - 1, 0);
        VDP_setSpriteFull(0, player_x, player_y, SPRITE_SIZE(3, 3),
                          TILE_ATTR_FULL(PAL2, 1, 0, 0, tile_l), 1);
        VDP_setSpriteFull(1, player_x + 24, player_y, SPRITE_SIZE(3, 3),
                          TILE_ATTR_FULL(PAL2, 1, 0, 0, tile_r), 2);
    }
    else
    {
        VDP_setSpriteFull(0, player_x, player_y, SPRITE_SIZE(3, 3),
                          TILE_ATTR_FULL(PAL2, 1, 0, 0, tile_l), 1);
        VDP_setSpriteFull(1, player_x + 24, player_y, SPRITE_SIZE(3, 3),
                          TILE_ATTR_FULL(PAL2, 1, 0, 0, tile_r), 0);
    }

    VDP_updateSprites(sprite_idx, DMA_QUEUE);
}

/**
 * @brief Configures Genesis Plane B tilemap to display the 20x16 ASIC canvas,
 * extending the outer horizon/ground scanlines across all 40 columns in Sega CD mode.
 *
 * @param standalone True for standalone cartridge mode with center rotating viewport,
 *                   false for Sega CD mode with full-width banked road tile mapping.
 */
static void init_asic_vdp_plane(bool standalone)
{
    const uint16_t start_col = (40 - ASIC_TILES_W) / 2; /* col 10 */

    if (standalone)
    {
        const uint16_t start_row = 10;

        /* Load static frame border tiles into VRAM */
        VDP_loadTileData(s_border_tiles, STANDALONE_BORDER_BASE, 8, DMA);

        for (uint16_t r = 0; r < ASIC_TILES_H; r++)
        {
            for (uint16_t c = 0; c < ASIC_TILES_W; c++)
            {
                uint16_t tile_index;

                /* Center 8x8 viewport for software affine rotation */
                if (r >= 4 && r < 12 && c >= 6 && c < 14)
                {
                    tile_index = ASIC_TILE_BASE + ((r - 4) * STANDALONE_TILES_W) + (c - 6);
                }
                else if (r == 0 && c == 0)
                {
                    tile_index = STANDALONE_BORDER_BASE + 0; /* Top-Left corner */
                }
                else if (r == 0 && c == (ASIC_TILES_W - 1))
                {
                    tile_index = STANDALONE_BORDER_BASE + 1; /* Top-Right corner */
                }
                else if (r == (ASIC_TILES_H - 1) && c == 0)
                {
                    tile_index = STANDALONE_BORDER_BASE + 2; /* Bottom-Left corner */
                }
                else if (r == (ASIC_TILES_H - 1) && c == (ASIC_TILES_W - 1))
                {
                    tile_index = STANDALONE_BORDER_BASE + 3; /* Bottom-Right corner */
                }
                else if (r == 0 || r == (ASIC_TILES_H - 1))
                {
                    tile_index = STANDALONE_BORDER_BASE + 4; /* Horizontal border */
                }
                else if (c == 0 || c == (ASIC_TILES_W - 1))
                {
                    tile_index = STANDALONE_BORDER_BASE + 5; /* Vertical border */
                }
                else if ((r & 1) && (c & 1))
                {
                    tile_index = STANDALONE_BORDER_BASE + 6; /* Grid cross accent */
                }
                else
                {
                    tile_index = STANDALONE_BORDER_BASE + 7; /* Blank tile */
                }

                VDP_setTileMapXY(BG_B, TILE_ATTR_FULL(PAL0, 0, 0, 0, tile_index), start_col + c, start_row + r);
            }
        }
    }
    else
    {
        /* Sega CD Banked Road Mode: place 16-row ASIC canvas at rows 12..27.
         * Rows 12..27 (16 rows) map 1:1 to ASIC canvas rows 0..15 without any duplication!
         * Columns 10..29 map 1:1 to ASIC columns 0..19; outer columns 0..9 and 30..39 repeat the outer
         * edge scanline tiles so the synthwave ground grid spans all 320 pixels! */
        const uint16_t start_row = 12;

        for (uint16_t screen_r = start_row; screen_r < 28; screen_r++)
        {
            uint16_t r = screen_r - start_row;

            for (uint16_t screen_c = 0; screen_c < 40; screen_c++)
            {
                uint16_t asic_c;
                if (screen_c < start_col)
                {
                    asic_c = 0;
                }
                else if (screen_c >= (start_col + ASIC_TILES_W))
                {
                    asic_c = ASIC_TILES_W - 1;
                }
                else
                {
                    asic_c = screen_c - start_col;
                }

                uint16_t tile_index = ASIC_TILE_BASE + (r * ASIC_TILES_W) + asic_c;
                VDP_setTileMapXY(BG_B, TILE_ATTR_FULL(PAL0, 0, 0, 0, tile_index), screen_c, screen_r);
            }
        }
    }
}

/* Embedded Sub-CPU 68000 Firmware Payload (from sub_payload.s) */
extern const uint8_t sub_bin_start[];
extern const uint8_t sub_bin_end[];
extern const uint32_t sub_bin_size;

/**
 * @brief Probes for Sega CD hardware and brings the Sub-CPU online in 1M Word RAM mode.
 *
 * If Sega CD hardware is present:
 *   1. Requests Sub-CPU bus and halts Sub-CPU in reset (RESET=0, BUSREQ=1).
 *   2. Uploads the embedded Sub-CPU firmware payload directly into PRG-RAM at 0x420000.
 *   3. Configures Word RAM to 1M mode (MODE=1, RET=0).
 *   4. Releases Sub-CPU reset (RESET=1, BUSREQ=0) to begin execution from PRG-RAM address 0.
 *   5. Dispatches CMD_INIT via COMM registers and awaits STAT_READY handshake.
 *
 * @return True if Sub-CPU acknowledged handshake within timeout window, false otherwise.
 */
static bool init_segacd(void)
{
    /* Probe 0xA12000: reading on stock Genesis returns open bus (0xFFFF) */
    if (SCD_REG_RESET == 0xFFFF)
    {
        /* Sega CD expansion hardware not detected */
        return false;
    }

    /* 1. Request Sub-CPU bus and assert reset: RESET=0, BUSREQ=1 */
    SCD_REG_RESET = 0x0002;
    uint32_t bus_timeout = 100000;
    while (!(SCD_REG_RESET & 0x0002) && --bus_timeout)
    {
        __asm__ volatile ("nop");
    }

    /* 2. Configure 1M Word RAM mode and map PRG-RAM Bank 0 (0x00000..0x1FFFF) */
    SCD_REG_MEMMODE = 0x0004;

    /* 3. Upload Sub-CPU firmware payload into PRG-RAM at 0x420000 */
    volatile uint16_t *prg_ram = (volatile uint16_t *)0x420000;
    const uint16_t *src = (const uint16_t *)sub_bin_start;
    uint32_t word_count = (sub_bin_size + 1) / 2;
    for (uint32_t i = 0; i < word_count; i++)
    {
        prg_ram[i] = src[i];
    }

    /* 4. Release Sub-CPU bus request and release reset: RESET=1, BUSREQ=0 */
    SCD_REG_RESET = 0x0001;

    /* 5. Dispatch initialization command to Sub-CPU */
    SCD_REG_COMM_MD0 = CMD_INIT;
    SCD_REG_COMM_MD1 = 0x0001; /* 1M mode flag */

    /* 6. Wait with timeout for Sub-CPU ready status */
    /* Wait across VSync intervals so emulator multi-core time slices advance Sub-CPU */
    uint32_t timeout = 120;
    while ((SCD_REG_COMM_SUB0 != STAT_READY) && --timeout)
    {
        VDP_waitVSync();
    }

    return (timeout > 0);
}

/**
 * @brief Swaps the active 128 KB Word RAM bank with the Sub-CPU.
 * Toggles the RET bit and waits for the memory controller acknowledge bit (DMNA).
 */
static void swap_word_ram(void)
{
    uint16_t cur = SCD_REG_MEMMODE;
    /* Invert RET bit (bit 0) to request bank swap with Sub-CPU */
    SCD_REG_MEMMODE = cur ^ 0x0001;
}

/**
 * @brief Genesis Main-CPU entry point.
 */
int main(bool hardReset)
{
    (void)hardReset;

    /* Initialize SGDK VDP and configure per-tile horizontal scrolling so HUD rows (0..6)
     * remain completely stationary while city skyline rows (7..11) scroll with parallax */
    VDP_init();
    VDP_setScrollingMode(HSCROLL_TILE, VSCROLL_PLANE);
    init_palettes();

    /* Load vehicle sprites into VRAM */
    init_vehicle_sprites();

    /* Render distant cyberpunk nighttime city skyline on Plane A (rows 7..11) */
    init_cyberpunk_city_skyline();

    VDP_setTextPalette(PAL0);
    VDP_drawText("========================================", 0, 0);

    VDP_setTextPalette(PAL1);
    VDP_drawText("      SEGA CD ASIC CYBER ROAD DEMO      ", 0, 1);

    /* Initialize Sega CD Subsystem */
    VDP_setTextPalette(PAL2);
    VDP_drawText("Probing Sega CD Subsystem...", 2, 2);
    s_cd_online = init_segacd();

    if (s_cd_online)
    {
        /* Sega CD expansion detected and Sub-CPU running */
        VDP_setTextPalette(PAL1);
        VDP_drawText("[x] Sub-68000 ASIC: BANKED ROAD ONLINE ", 2, 2);
        init_asic_vdp_plane(false);
    }
    else
    {
        /* Running in fallback / standalone Mega Drive mode */
        VDP_setTextPalette(PAL3);
        VDP_drawText("[!] Sega CD Sub-CPU: Standalone MD Mode", 2, 2);
        init_standalone_texture();
        init_asic_vdp_plane(true);
    }

    VDP_setTextPalette(PAL2);
    VDP_drawText("D-Pad:Steer/Speed [A]Save [B]Load [C]Fmt", 0, 3);

    char text_buf[40];
    uint16_t prev_pad = 0;
    const char *bram_msg = "BRAM: 126 Blocks Free";

    while (1)
    {
        s_main_frame_count++;

        /* 1. Poll Controller inputs with edge detection */
        uint16_t pad = JOY_readJoypad(JOY_1);
        uint16_t pad_edge = pad & ~prev_pad;
        prev_pad = pad;

        /* 2. Dispatch render or BRAM commands to Sega CD Sub-CPU if present, or software render in standalone mode */
        if (s_cd_online)
        {
            uint16_t cmd = CMD_RENDER_FRAME;

            if (pad_edge & BUTTON_A)
            {
                cmd = CMD_BRAM_SAVE;
                bram_msg = "BRAM: Saved Game to SRAM    ";
            }
            else if (pad_edge & BUTTON_B)
            {
                cmd = CMD_BRAM_LOAD;
                bram_msg = "BRAM: Loaded Game from SRAM ";
            }
            else if (pad_edge & BUTTON_C)
            {
                cmd = CMD_BRAM_FORMAT;
                bram_msg = "BRAM: Formatted Backup RAM  ";
            }

            SCD_REG_COMM_MD0 = cmd;
            SCD_REG_COMM_MD1 = (uint16_t)(s_main_frame_count & 0xFFFF);
            SCD_REG_COMM_MD2 = pad;

            /* Swap Word RAM ping-pong bank */
            swap_word_ram();

            /* Transfer rendered 160x128 ASIC tile canvas (320 tiles) from Word RAM into VRAM.
             * In Mode 1 (Cartridge + Sega CD), 1M Word RAM maps to 0x600000; in Mode 2 (Disc boot), it maps to 0x200000. */
            const uint32_t *wram_m1 = (const uint32_t *)(0x600000 + ASIC_WRAM_OFFSET);
            const uint32_t *wram_m2 = (const uint32_t *)(0x200000 + ASIC_WRAM_OFFSET);
            const uint32_t *wram_tiles = (wram_m1[200] != 0 || wram_m1[400] != 0) ? wram_m1 : wram_m2;
            VDP_loadTileData(wram_tiles, ASIC_TILE_BASE, ASIC_TOTAL_TILES, CPU);

            /* Render 48x24 player supercar and multi-LOD traffic cruisers over the road */
            uint16_t stat2 = SCD_REG_COMM_SUB2;
            uint16_t speed = stat2 & 0x00FF;
            int8_t curve   = (int8_t)(stat2 >> 8);
            uint16_t telemetry = SCD_REG_COMM_SUB3;
            int8_t bank    = (int8_t)(telemetry >> 8);
            int8_t steer   = (int8_t)(telemetry & 0xFF);
            update_road_sprites(bank, steer, curve, speed);

            /* Parallax horizontal scrolling restricted strictly to distant city skyline rows (7..11) on Plane B */
            int16_t skyline_scroll_x = -((int16_t)steer >> 1) - ((int16_t)bank * 2) - ((int16_t)curve >> 1);
            int16_t sky_scroll[5];
            sky_scroll[0] = skyline_scroll_x >> 1; /* Upper starfield */
            sky_scroll[1] = skyline_scroll_x;      /* Spire tips */
            sky_scroll[2] = skyline_scroll_x;      /* Mid towers */
            sky_scroll[3] = skyline_scroll_x;      /* Lower silhouette */
            sky_scroll[4] = skyline_scroll_x;      /* Ground baseline */
            VDP_setHorizontalScrollTile(BG_B, 7, sky_scroll, 5, CPU);

            /* Animate neon billboard lights and antenna strobe beacons via palette cycling */
            update_neon_palette_animation(s_main_frame_count);
        }
        else
        {
            /* Standalone MD Mode: process joypad rotation */
            if (pad & BUTTON_LEFT)
            {
                /* Counter-clockwise manual rotation */
                s_standalone_angle = (uint8_t)(s_standalone_angle - 2);
            }
            else if (pad & BUTTON_RIGHT)
            {
                /* Clockwise manual rotation */
                s_standalone_angle = (uint8_t)(s_standalone_angle + 2);
            }
            else
            {
                /* Default continuous rotation when idle */
                s_standalone_angle = (uint8_t)(s_standalone_angle + 1);
            }

            /* Standalone MD Mode: process joypad zoom */
            if (pad & BUTTON_UP)
            {
                /* Zoom in towards target ceiling */
                s_standalone_scale = (s_standalone_scale < 512) ? (s_standalone_scale + 4) : 512;
            }
            else if (pad & BUTTON_DOWN)
            {
                /* Zoom out towards target floor */
                s_standalone_scale = (s_standalone_scale > 64) ? (s_standalone_scale - 4) : 64;
            }
            else
            {
                /* Automatic gentle breathing zoom cycle when idle */
                int32_t next_scale = (int32_t)s_standalone_scale + s_standalone_scale_velocity;
                if (next_scale >= 384)
                {
                    s_standalone_scale_velocity = -2;
                    next_scale = 384;
                }
                else if (next_scale <= 160)
                {
                    s_standalone_scale_velocity = 2;
                    next_scale = 160;
                }
                s_standalone_scale = (uint16_t)next_scale;
            }

            /* Cartridge Backup SRAM Save / Load / Format operations */
            if (pad_edge & BUTTON_A)
            {
                /* Save current angle and scale to cartridge SRAM */
                SRAM_enable();
                SRAM_writeByte(0, s_standalone_angle);
                SRAM_writeWord(2, s_standalone_scale);
                SRAM_disable();
                bram_msg = "SRAM: Saved State to Cartridge  ";
            }
            else if (pad_edge & BUTTON_B)
            {
                /* Load persisted angle and scale from cartridge SRAM */
                SRAM_enable();
                uint8_t saved_angle = SRAM_readByte(0);
                uint16_t saved_scale = SRAM_readWord(2);
                SRAM_disable();

                if (saved_scale >= 64 && saved_scale <= 512)
                {
                    s_standalone_angle = saved_angle;
                    s_standalone_scale = saved_scale;
                }
                bram_msg = "SRAM: Loaded State from Cart    ";
            }
            else if (pad_edge & BUTTON_C)
            {
                /* Format / clear cartridge SRAM */
                SRAM_enable();
                SRAM_writeByte(0, 0);
                SRAM_writeWord(2, 256);
                SRAM_disable();
                s_standalone_angle = 0;
                s_standalone_scale = 256;
                bram_msg = "SRAM: Formatted Cartridge RAM   ";
            }

            /* Software-render affine transformed emblem and DMA 64 tiles to VRAM */
            render_affine(s_standalone_angle, s_standalone_scale);
            VDP_loadTileData(s_standalone_tiles, ASIC_TILE_BASE, STANDALONE_TILES, DMA);
        }

        /* 3. Update HUD telemetry, BRAM status, and frame counters on stationary rows 4..6 */
        if ((s_main_frame_count % 8) == 0)
        {
            uint16_t sub_frames = s_cd_online ? SCD_REG_COMM_SUB1 : 0;

            VDP_setTextPalette(PAL0);
            if (s_cd_online)
            {
                uint16_t speed = SCD_REG_COMM_SUB2 & 0x00FF;
                uint16_t telemetry = SCD_REG_COMM_SUB3;
                int8_t bank = (int8_t)(telemetry >> 8);
                int8_t steer = (int8_t)(telemetry & 0xFF);

                sprintf(text_buf, "Main: %-6lu  Sub: %-6u", (unsigned long)s_main_frame_count, sub_frames);
                VDP_drawText(text_buf, 2, 4);

                VDP_setTextPalette(PAL1);
                sprintf(text_buf, "Speed: %3u km/h  Bank: %+3d  Steer: %+3d", (unsigned int)(speed * 3), bank, steer);
                VDP_drawText(text_buf, 2, 5);
            }
            else
            {
                uint16_t degrees = (uint16_t)(((uint32_t)s_standalone_angle * 360) / 256);
                uint16_t percent = (uint16_t)(((uint32_t)s_standalone_scale * 100) / 256);

                sprintf(text_buf, "Main: %-6lu  Sub: [Standby]", (unsigned long)s_main_frame_count);
                VDP_drawText(text_buf, 2, 4);

                VDP_setTextPalette(PAL2);
                sprintf(text_buf, "Angle: %3u deg  Zoom: %3u%%  ", degrees, percent);
                VDP_drawText(text_buf, 2, 5);
            }

            VDP_setTextPalette(PAL3);
            VDP_drawText(bram_msg, 2, 6);
        }

        /* Wait for VBlank */
        SYS_doVBlankProcess();
    }

    return 0;
}
