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

/* ASIC Display Canvas Dimensions */
#define ASIC_TILE_BASE       16
#define ASIC_TILES_W         20   /* 160 pixels / 8 */
#define ASIC_TILES_H         16   /* 128 pixels / 8 */
#define ASIC_TOTAL_TILES     (ASIC_TILES_W * ASIC_TILES_H)
#define ASIC_WRAM_OFFSET     0x1000

/* Standalone Mega Drive Viewport Dimensions: 8x8 tiles (64x64 pixels) */
#define STANDALONE_TILES_W   8
#define STANDALONE_TILES_H   8
#define STANDALONE_TILES     (STANDALONE_TILES_W * STANDALONE_TILES_H)
#define STANDALONE_BORDER_BASE (ASIC_TILE_BASE + STANDALONE_TILES)

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
 * @brief Initializes the Genesis VDP color palettes with distinct entries.
 * Configures PAL0 for both ASIC tile graphics and text, and PAL1-3 for HUD accents.
 */
static void init_palettes(void)
{
    /* Backdrop: Deep navy blue across all palettes */
    PAL_setColor(0,       RGB24_TO_VDPCOLOR(0x000033));

    /* PAL0: Dedicated ASIC stamp canvas and primary UI text */
    PAL_setColor(1,       RGB24_TO_VDPCOLOR(0x00ff88)); /* Stamp outer border (emerald green) */
    PAL_setColor(2,       RGB24_TO_VDPCOLOR(0xfbbf24)); /* Stamp middle ring (amber gold) */
    PAL_setColor(3,       RGB24_TO_VDPCOLOR(0xf43f5e)); /* Stamp center cross (ruby crimson) */
    PAL_setColor(15,      RGB24_TO_VDPCOLOR(0xffffff)); /* Primary text (pure white) */

    /* PAL1..3: Status banners and diagnostic accents */
    PAL_setColor(16 + 15, RGB24_TO_VDPCOLOR(0xfbbf24)); /* PAL1 text (amber) */
    PAL_setColor(32 + 15, RGB24_TO_VDPCOLOR(0x38bdf8)); /* PAL2 text (cyan) */
    PAL_setColor(48 + 15, RGB24_TO_VDPCOLOR(0xf43f5e)); /* PAL3 text (ruby) */
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
 * @brief Decorative static border tiles for Standalone MD Mode canvas framing.
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

/**
 * @brief Configures Genesis Plane B tilemap to display the 20x16 tile canvas.
 *
 * @param standalone True for standalone cartridge mode with center rotating viewport,
 *                   false for Sega CD mode with full 20x16 Word RAM tile mapping.
 */
static void init_asic_vdp_plane(bool standalone)
{
    /* Center the 20x16 tile canvas on 40x28 Plane B (horizontal col 10, vertical row 10) */
    const uint16_t start_col = (40 - ASIC_TILES_W) / 2;
    const uint16_t start_row = 10;

    if (standalone)
    {
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
        /* Sega CD 20x16 contiguous ASIC Word RAM tile mapping */
        for (uint16_t r = 0; r < ASIC_TILES_H; r++)
        {
            for (uint16_t c = 0; c < ASIC_TILES_W; c++)
            {
                uint16_t tile_index = ASIC_TILE_BASE + (r * ASIC_TILES_W) + c;
                VDP_setTileMapXY(BG_B, TILE_ATTR_FULL(PAL0, 0, 0, 0, tile_index), start_col + c, start_row + r);
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
 *   2. Uploads the embedded Sub-CPU firmware payload directly into PRG-RAM at 0x400000.
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

    /* 3. Upload Sub-CPU firmware payload into PRG-RAM at 0x400000 */
    volatile uint16_t *prg_ram = (volatile uint16_t *)0x400000;
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
    uint32_t timeout = 500000;
    while ((SCD_REG_COMM_SUB0 != STAT_READY) && --timeout)
    {
        __asm__ volatile ("nop");
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
    /* Invert RET bit (bit 0) */
    SCD_REG_MEMMODE = cur ^ 0x0001;

    /* Poll for DMNA bit to verify swap completion */
    uint32_t timeout = 100000;
    while (!(SCD_REG_MEMMODE & 0x0002) && --timeout)
    {
        __asm__ volatile ("nop");
    }
}

/**
 * @brief Genesis Main-CPU entry point.
 */
int main(bool hardReset)
{
    (void)hardReset;

    /* Initialize SGDK VDP and text plane */
    VDP_init();
    init_palettes();

    VDP_setTextPalette(PAL0);
    VDP_drawText("========================================", 0, 1);

    VDP_setTextPalette(PAL1);
    VDP_drawText("      SEGA CD TURNKEY DEV PIPELINE      ", 0, 3);

    VDP_setTextPalette(PAL0);
    VDP_drawText("----------------------------------------", 0, 4);

    /* Initialize Sega CD Subsystem */
    VDP_setTextPalette(PAL2);
    VDP_drawText("Probing Sega CD Subsystem...", 2, 5);
    s_cd_online = init_segacd();

    if (s_cd_online)
    {
        /* Sega CD expansion detected and Sub-CPU running */
        VDP_setTextPalette(PAL0);
        VDP_drawText("[x] Sub-68000 @ 12.5 MHz: ASIC ACTIVE ", 2, 5);
        init_asic_vdp_plane(false);
    }
    else
    {
        /* Running in fallback / standalone Mega Drive mode */
        VDP_setTextPalette(PAL3);
        VDP_drawText("[!] Sega CD Sub-CPU: Standalone MD Mode", 2, 5);
        init_standalone_texture();
        init_asic_vdp_plane(true);
    }

    VDP_setTextPalette(PAL1);
    VDP_drawText("D-Pad:Rot/Zoom  [A]Save [B]Load [C]Fmt", 2, 8);

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

            /* DMA rendered 160x128 ASIC tile canvas (320 tiles) directly into VRAM */
            const uint32_t *wram_tiles = (const uint32_t *)(0x200000 + ASIC_WRAM_OFFSET);
            VDP_loadTileData(wram_tiles, ASIC_TILE_BASE, ASIC_TOTAL_TILES, DMA);
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

        /* 3. Update HUD telemetry, BRAM status, and frame counters */
        if ((s_main_frame_count % 8) == 0)
        {
            uint16_t sub_frames = s_cd_online ? SCD_REG_COMM_SUB1 : 0;
            uint16_t raw_angle  = s_cd_online ? SCD_REG_COMM_SUB2 : s_standalone_angle;
            uint16_t raw_scale  = s_cd_online ? SCD_REG_COMM_SUB3 : s_standalone_scale;

            uint16_t degrees = (uint16_t)(((uint32_t)raw_angle * 360) / 256);
            uint16_t percent = (uint16_t)(((uint32_t)raw_scale * 100) / 256);

            VDP_setTextPalette(PAL0);
            if (s_cd_online)
            {
                sprintf(text_buf, "Main: %-6lu  Sub: %-6u", (unsigned long)s_main_frame_count, sub_frames);
            }
            else
            {
                sprintf(text_buf, "Main: %-6lu  Sub: [Standby]", (unsigned long)s_main_frame_count);
            }
            VDP_drawText(text_buf, 2, 6);

            VDP_setTextPalette(PAL2);
            sprintf(text_buf, "Angle: %3u deg  Zoom: %3u%%  ", degrees, percent);
            VDP_drawText(text_buf, 2, 7);

            VDP_setTextPalette(PAL3);
            VDP_drawText(bram_msg, 2, 9);
        }

        /* Wait for VBlank */
        SYS_doVBlankProcess();
    }

    return 0;
}
