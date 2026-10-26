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

static bool s_cd_online = false;
static uint32_t s_main_frame_count = 0;

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
 * @brief Configures Genesis Plane B tilemap to display the 20x16 tile ASIC canvas.
 */
static void init_asic_vdp_plane(void)
{
    /* Center the 20x16 tile canvas on 40x28 Plane B (horizontal col 10, vertical row 10) */
    const uint16_t start_col = (40 - ASIC_TILES_W) / 2;
    const uint16_t start_row = 10;

    for (uint16_t r = 0; r < ASIC_TILES_H; r++)
    {
        for (uint16_t c = 0; c < ASIC_TILES_W; c++)
        {
            uint16_t tile_index = ASIC_TILE_BASE + (r * ASIC_TILES_W) + c;
            VDP_setTileMapXY(BG_B, TILE_ATTR_FULL(PAL0, 0, 0, 0, tile_index), start_col + c, start_row + r);
        }
    }
}

/**
 * @brief Probes for Sega CD hardware and brings the Sub-CPU online in 1M Word RAM mode.
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

    /* Release Sub-CPU reset and grant bus: RESET=1, BUSREQ=0 */
    SCD_REG_RESET = 0x0001;

    /* Configure 1M Word RAM mode (MODE=1, RET=0) */
    SCD_REG_MEMMODE = 0x0004;

    /* Dispatch initialization command to Sub-CPU */
    SCD_REG_COMM_MD0 = CMD_INIT;
    SCD_REG_COMM_MD1 = 0x0001; /* 1M mode flag */

    /* Wait with timeout for Sub-CPU ready status */
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
        init_asic_vdp_plane();
    }
    else
    {
        /* Running in fallback / standalone Mega Drive mode */
        VDP_setTextPalette(PAL3);
        VDP_drawText("[!] Sega CD Sub-CPU: Standalone MD Mode", 2, 5);
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

        /* 2. Dispatch render or BRAM commands to Sega CD Sub-CPU if present */
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

        /* 3. Update HUD telemetry, BRAM status, and frame counters */
        if ((s_main_frame_count % 8) == 0)
        {
            uint16_t sub_frames = s_cd_online ? SCD_REG_COMM_SUB1 : 0;
            uint16_t raw_angle  = s_cd_online ? SCD_REG_COMM_SUB2 : 0;
            uint16_t raw_scale  = s_cd_online ? SCD_REG_COMM_SUB3 : 256;

            uint16_t degrees = (uint16_t)(((uint32_t)raw_angle * 360) / 256);
            uint16_t percent = (uint16_t)(((uint32_t)raw_scale * 100) / 256);

            VDP_setTextPalette(PAL0);
            sprintf(text_buf, "Main: %-6lu  Sub: %-6u", (unsigned long)s_main_frame_count, sub_frames);
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
