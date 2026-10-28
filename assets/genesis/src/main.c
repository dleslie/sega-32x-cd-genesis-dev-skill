/**
 * @file main.c
 * @brief Sega Genesis Turnkey Sprite Engine Showcase.
 *
 * Demonstrates the Sega Genesis VDP Hardware Sprite Engine featuring:
 *   - Real-time sprite scaling across all 4 hardware sizes (8x8, 16x16, 24x24, 32x32)
 *   - Dual interactive demonstration modes:
 *       1. 3D DNA Double Helix with depth-scaled nucleotide spheres & base pairs
 *       2. Mode-7 Style Infinite Scaling Forest with perspective ground plane
 *   - Hardware sprite depth sorting (SAT priority: lower index on top)
 *   - 100% pure Genesis hardware (Motorola 68000 @ 7.67 MHz + VDP)
 *
 * Copyright (c) 2026 Dan Leslie & Stéphane Dallongeville
 * SPDX-License-Identifier: MIT
 */

#include <genesis.h>
#include "genesis_tiles.h"

/* VRAM Tile Offsets */
#define TILE_BASE_INDEX     TILE_USER_INDEX
#define TILE_SPHERE_8       (TILE_BASE_INDEX)
#define TILE_SPHERE_16      (TILE_SPHERE_8 + 1)
#define TILE_SPHERE_24      (TILE_SPHERE_16 + 4)
#define TILE_SPHERE_32      (TILE_SPHERE_24 + 9)
#define TILE_BAR_8          (TILE_SPHERE_32 + 16)
#define TILE_TREE_8         (TILE_BAR_8 + 1)
#define TILE_TREE_16        (TILE_TREE_8 + 1)
#define TILE_TREE_24        (TILE_TREE_16 + 4)
#define TILE_TREE_32        (TILE_TREE_24 + 9)
#define TILE_GROUND         (TILE_TREE_32 + 16)

#define DEMO_MODE_DNA       0
#define DEMO_MODE_FOREST    1

#define NUM_DNA_RUNGS       16
#define NUM_DNA_SPRITES     (NUM_DNA_RUNGS * 3) /* 16 Node A + 16 Node B + 16 Rungs = 48 */
#define NUM_FOREST_TREES    36

/* Sine lookup table (256 entries, amplitude [-127..127]) */
static const s8 s_sin_tab[256] = {
      0,   3,   6,   9,  12,  15,  18,  21,  24,  28,  31,  34,  37,  40,  43,  46,
     48,  51,  54,  57,  60,  63,  65,  68,  71,  73,  76,  78,  81,  83,  85,  88,
     90,  92,  94,  96,  98, 100, 102, 104, 106, 108, 109, 111, 112, 114, 115, 117,
    118, 119, 120, 121, 122, 123, 124, 124, 125, 126, 126, 126, 127, 127, 127, 127,
    127, 127, 127, 127, 127, 126, 126, 126, 125, 124, 124, 123, 122, 121, 120, 119,
    118, 117, 115, 114, 112, 111, 109, 108, 106, 104, 102, 100,  98,  96,  94,  92,
     90,  88,  85,  83,  81,  78,  76,  73,  71,  68,  65,  63,  60,  57,  54,  51,
     48,  46,  43,  40,  37,  34,  31,  28,  24,  21,  18,  15,  12,   9,   6,   3,
      0,  -3,  -6,  -9, -12, -15, -18, -21, -24, -28, -31, -34, -37, -40, -43, -46,
    -48, -51, -54, -57, -60, -63, -65, -68, -71, -73, -76, -78, -81, -83, -85, -88,
    -90, -92, -94, -96, -98,-100,-102,-104,-106,-108,-109,-111,-112,-114,-115,-117,
   -118,-119,-120,-121,-122,-123,-124,-124,-125,-126,-126,-126,-127,-127,-127,-127,
   -127,-127,-127,-127,-127,-126,-126,-126,-125,-124,-124,-123,-122,-121,-120,-119,
   -118,-117,-115,-114,-112,-111,-109,-108,-106,-104,-102,-100, -98, -96, -94, -92,
    -90, -88, -85, -83, -81, -78, -76, -73, -71, -68, -65, -63, -60, -57, -54, -51,
    -48, -46, -43, -40, -37, -34, -31, -28, -24, -21, -18, -15, -12,  -9,  -6,  -3
};

static inline s16 fast_sin(u8 angle) {
    return s_sin_tab[angle];
}

static inline s16 fast_cos(u8 angle) {
    return s_sin_tab[(u8)(angle + 64)];
}

/* Palette 0: Far Depth & UI Text */
static const u16 s_pal0[16] = {
    RGB24_TO_VDPCOLOR(0x060814), /* Backdrop Navy */
    RGB24_TO_VDPCOLOR(0x001828), /* Sphere Edge 1 */
    RGB24_TO_VDPCOLOR(0x003348), /* Sphere Body 2 */
    RGB24_TO_VDPCOLOR(0x005570), /* Sphere Mid  3 */
    RGB24_TO_VDPCOLOR(0x117799), /* Sphere Spec 4 */
    RGB24_TO_VDPCOLOR(0x002200), /* Tree Shadow 5 */
    RGB24_TO_VDPCOLOR(0x004411), /* Tree Mid    6 */
    RGB24_TO_VDPCOLOR(0x116622), /* Tree High   7 */
    RGB24_TO_VDPCOLOR(0x332211), /* Tree Trunk  8 */
    RGB24_TO_VDPCOLOR(0x222233), /* Bar Shadow  9 */
    RGB24_TO_VDPCOLOR(0x444466), /* Bar Core   10 */
    RGB24_TO_VDPCOLOR(0x442200), /* Gold Text  11 */
    RGB24_TO_VDPCOLOR(0x0088aa), /* Cyan Accent12 */
    RGB24_TO_VDPCOLOR(0x00ddff), /* Cyan Bright13 */
    RGB24_TO_VDPCOLOR(0xffcc00), /* Gold Bright14 */
    RGB24_TO_VDPCOLOR(0xffffff)  /* Pure White 15 */
};

/* Palette 1: Mid-Back Depth */
static const u16 s_pal1[16] = {
    RGB24_TO_VDPCOLOR(0x000000), /* Transparent */
    RGB24_TO_VDPCOLOR(0x00283c),
    RGB24_TO_VDPCOLOR(0x005578),
    RGB24_TO_VDPCOLOR(0x0088b4),
    RGB24_TO_VDPCOLOR(0x33b4e6),
    RGB24_TO_VDPCOLOR(0x003311),
    RGB24_TO_VDPCOLOR(0x006622),
    RGB24_TO_VDPCOLOR(0x229933),
    RGB24_TO_VDPCOLOR(0x553311),
    RGB24_TO_VDPCOLOR(0x333344),
    RGB24_TO_VDPCOLOR(0x777799),
    RGB24_TO_VDPCOLOR(0x441122),
    RGB24_TO_VDPCOLOR(0x772233),
    RGB24_TO_VDPCOLOR(0xaa3355),
    RGB24_TO_VDPCOLOR(0xdd5577),
    RGB24_TO_VDPCOLOR(0xffffff)
};

/* Palette 2: Mid-Front Depth */
static const u16 s_pal2[16] = {
    RGB24_TO_VDPCOLOR(0x000000), /* Transparent */
    RGB24_TO_VDPCOLOR(0x004466),
    RGB24_TO_VDPCOLOR(0x0088b4),
    RGB24_TO_VDPCOLOR(0x00bbdd),
    RGB24_TO_VDPCOLOR(0x66ddff),
    RGB24_TO_VDPCOLOR(0x004411),
    RGB24_TO_VDPCOLOR(0x008822),
    RGB24_TO_VDPCOLOR(0x33cc44),
    RGB24_TO_VDPCOLOR(0x774422),
    RGB24_TO_VDPCOLOR(0x444455),
    RGB24_TO_VDPCOLOR(0xaaaacc),
    RGB24_TO_VDPCOLOR(0x771122),
    RGB24_TO_VDPCOLOR(0xbb2244),
    RGB24_TO_VDPCOLOR(0xee3355),
    RGB24_TO_VDPCOLOR(0xff6688),
    RGB24_TO_VDPCOLOR(0xffffff)
};

/* Palette 3: Foreground High-Intensity Specular Highlights */
static const u16 s_pal3[16] = {
    RGB24_TO_VDPCOLOR(0x000000), /* Transparent */
    RGB24_TO_VDPCOLOR(0x006699),
    RGB24_TO_VDPCOLOR(0x00bbee),
    RGB24_TO_VDPCOLOR(0x66eeff),
    RGB24_TO_VDPCOLOR(0xffffff), /* Pure Specular Core */
    RGB24_TO_VDPCOLOR(0x005511),
    RGB24_TO_VDPCOLOR(0x00aa33),
    RGB24_TO_VDPCOLOR(0x44ff55),
    RGB24_TO_VDPCOLOR(0x995522),
    RGB24_TO_VDPCOLOR(0x666677),
    RGB24_TO_VDPCOLOR(0xeeeeff),
    RGB24_TO_VDPCOLOR(0xaa2244),
    RGB24_TO_VDPCOLOR(0xee3355),
    RGB24_TO_VDPCOLOR(0xff7799),
    RGB24_TO_VDPCOLOR(0xffffff),
    RGB24_TO_VDPCOLOR(0xffffff)
};

/* Sprite render element for depth sorting */
typedef struct {
    s16 x;
    s16 y;
    s16 z;
    u8 size;
    u16 attr;
} RenderSprite;

/* Forest Tree State */
typedef struct {
    s16 x;
    s16 z;
} ForestTree;

static ForestTree s_trees[NUM_FOREST_TREES];
static RenderSprite s_sorted_sprites[64];

/* Simulation Variables */
static u8  s_demo_mode      = DEMO_MODE_DNA;
static u8  s_dna_angle      = 0;
static u8  s_wave_angle     = 0;
static s16 s_dna_radius     = 54;
static bool s_wave_enabled  = TRUE;

static s16 s_cam_x          = 0;
static s16 s_forest_speed   = 5;
static u16 s_ground_scroll  = 0;
static u32 s_frame_count    = 0;

/**
 * @brief Initializes VRAM tiles and hardware palettes.
 */
static void init_vdp(void)
{
    VDP_setScreenWidth320();
    VDP_setScreenHeight224();

    PAL_setColors(0,  s_pal0, 16, DMA);
    PAL_setColors(16, s_pal1, 16, DMA);
    PAL_setColors(32, s_pal2, 16, DMA);
    PAL_setColors(48, s_pal3, 16, DMA);

    /* Upload sphere sprite tiles */
    VDP_loadTileData(s_tile_sphere_8x8,   TILE_SPHERE_8,   1,  DMA);
    VDP_loadTileData(s_tile_sphere_16x16, TILE_SPHERE_16,  4,  DMA);
    VDP_loadTileData(s_tile_sphere_24x24, TILE_SPHERE_24,  9,  DMA);
    VDP_loadTileData(s_tile_sphere_32x32, TILE_SPHERE_32, 16,  DMA);

    /* Upload connector bar tile */
    VDP_loadTileData(s_tile_bar_8x8,      TILE_BAR_8,      1,  DMA);

    /* Upload pine tree sprite tiles */
    VDP_loadTileData(s_tile_tree_8x8,     TILE_TREE_8,     1,  DMA);
    VDP_loadTileData(s_tile_tree_16x16,   TILE_TREE_16,    4,  DMA);
    VDP_loadTileData(s_tile_tree_24x24,   TILE_TREE_24,    9,  DMA);
    VDP_loadTileData(s_tile_tree_32x32,   TILE_TREE_32,   16,  DMA);

    /* Upload alternating perspective ground pattern */
    static const u32 ground_strip[8] = {
        0x55555555, 0x55555555, 0x66666666, 0x66666666,
        0x55555555, 0x55555555, 0x88888888, 0x88888888
    };
    VDP_loadTileData(ground_strip, TILE_GROUND, 1, DMA);

    VDP_resetSprites();
}

/**
 * @brief Initializes forest tree positions across the infinite plane.
 */
static void init_forest(void)
{
    for (int i = 0; i < NUM_FOREST_TREES; i++)
    {
        s_trees[i].x = ((i * 47) % 360) - 180;
        s_trees[i].z = 24 + ((i * 300) / NUM_FOREST_TREES);
    }
}

/**
 * @brief Draws user interface header and control hints.
 */
static void update_hud(void)
{
    VDP_setTextPalette(PAL0);

    if (s_demo_mode == DEMO_MODE_DNA)
    {
        VDP_drawText("========================================", 0, 0);
        VDP_drawText("    SEGA GENESIS SPRITE ENGINE DEMO     ", 0, 1);
        VDP_drawText(" MODE: 3D DNA HELIX   [START: SWITCH]   ", 0, 2);
        VDP_drawText(" SPRITES: 48 / 80     SIZES: 8..32px    ", 0, 3);
        VDP_drawText("========================================", 0, 4);

        VDP_drawText("D-PAD:ROT/ZOOM  [A]:WAVE  [B]:RADIUS    ", 0, 27);
    }
    else
    {
        VDP_drawText("========================================", 0, 0);
        VDP_drawText("    SEGA GENESIS SPRITE ENGINE DEMO     ", 0, 1);
        VDP_drawText(" MODE: INFINITE FOREST [START: SWITCH]  ", 0, 2);
        VDP_drawText(" SPRITES: 36 / 80     SIZES: 8..32px    ", 0, 3);
        VDP_drawText("========================================", 0, 4);

        VDP_drawText("D-PAD:STEER/SPEED  [A]:TURBO  [B]:BRAKE ", 0, 27);
    }
}

/**
 * @brief Clears playfield between mode switches.
 */
static void switch_mode(void)
{
    VDP_clearSprites();
    VDP_clearPlane(BG_A, TRUE);
    VDP_clearPlane(BG_B, TRUE);

    s_demo_mode ^= 1;
    if (s_demo_mode == DEMO_MODE_FOREST)
    {
        init_forest();
        /* Fill lower half with ground plane tiles */
        for (int y = 14; y < 27; y++)
        {
            for (int x = 0; x < 40; x++)
            {
                VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(PAL0, 0, 0, 0, TILE_GROUND), x, y);
            }
        }
    }

    update_hud();
}

/**
 * @brief Updates and renders 3D DNA Double Helix.
 */
static void update_dna_helix(void)
{
    int count = 0;

    for (int i = 0; i < NUM_DNA_RUNGS; i++)
    {
        s16 y_pos = 46 + (i * 9);
        u8 rung_angle = s_dna_angle + (i * 14);

        s16 cos_val = fast_cos(rung_angle);
        s16 sin_val = fast_sin(rung_angle);

        s16 center_x = 160;
        if (s_wave_enabled)
        {
            center_x += (fast_sin(s_wave_angle + (i * 12)) * 32) / 128;
        }

        s16 dx = (cos_val * s_dna_radius) / 128;
        s16 dz = (sin_val * s_dna_radius) / 128;

        /* Node A (Strand Alpha) */
        s16 xa = center_x + dx;
        s16 za = dz;

        /* Node B (Strand Beta) */
        s16 xb = center_x - dx;
        s16 zb = -dz;

        /* Assign Node A */
        {
            u8 size; u16 tile; u8 pal; s16 offset;
            if (za < -20)      { size = SPRITE_SIZE(1, 1); tile = TILE_SPHERE_8;  pal = PAL0; offset = 4;  }
            else if (za < 8)   { size = SPRITE_SIZE(2, 2); tile = TILE_SPHERE_16; pal = PAL1; offset = 8;  }
            else if (za < 32)  { size = SPRITE_SIZE(3, 3); tile = TILE_SPHERE_24; pal = PAL2; offset = 12; }
            else               { size = SPRITE_SIZE(4, 4); tile = TILE_SPHERE_32; pal = PAL3; offset = 16; }

            s_sorted_sprites[count].x = xa - offset;
            s_sorted_sprites[count].y = y_pos - offset;
            s_sorted_sprites[count].z = za;
            s_sorted_sprites[count].size = size;
            s_sorted_sprites[count].attr = TILE_ATTR_FULL(pal, 0, 0, 0, tile);
            count++;
        }

        /* Assign Node B */
        {
            u8 size; u16 tile; u8 pal; s16 offset;
            if (zb < -20)      { size = SPRITE_SIZE(1, 1); tile = TILE_SPHERE_8;  pal = PAL0; offset = 4;  }
            else if (zb < 8)   { size = SPRITE_SIZE(2, 2); tile = TILE_SPHERE_16; pal = PAL1; offset = 8;  }
            else if (zb < 32)  { size = SPRITE_SIZE(3, 3); tile = TILE_SPHERE_24; pal = PAL2; offset = 12; }
            else               { size = SPRITE_SIZE(4, 4); tile = TILE_SPHERE_32; pal = PAL3; offset = 16; }

            s_sorted_sprites[count].x = xb - offset;
            s_sorted_sprites[count].y = y_pos - offset;
            s_sorted_sprites[count].z = zb;
            s_sorted_sprites[count].size = size;
            s_sorted_sprites[count].attr = TILE_ATTR_FULL(pal, 0, 0, 0, tile);
            count++;
        }

        /* Base-pair connector rung at midpoint */
        {
            s_sorted_sprites[count].x = ((xa + xb) / 2) - 4;
            s_sorted_sprites[count].y = y_pos - 4;
            s_sorted_sprites[count].z = 0; /* Mid-plane */
            s_sorted_sprites[count].size = SPRITE_SIZE(1, 1);
            s_sorted_sprites[count].attr = TILE_ATTR_FULL(PAL1, 0, 0, 0, TILE_BAR_8);
            count++;
        }
    }

    /* Insertion sort: descending Z (nearest / largest Z in Slot 0 has highest SAT priority) */
    for (int i = 1; i < count; i++)
    {
        RenderSprite key = s_sorted_sprites[i];
        int j = i - 1;
        while (j >= 0 && s_sorted_sprites[j].z < key.z)
        {
            s_sorted_sprites[j + 1] = s_sorted_sprites[j];
            j--;
        }
        s_sorted_sprites[j + 1] = key;
    }

    /* Dispatch to VDP Sprite Cache */
    for (int i = 0; i < count; i++)
    {
        u8 link = (i == count - 1) ? 0 : (i + 1);
        VDP_setSpriteFull(i, s_sorted_sprites[i].x, s_sorted_sprites[i].y,
                          s_sorted_sprites[i].size, s_sorted_sprites[i].attr, link);
    }
    VDP_updateSprites(count, DMA_QUEUE);

    s_dna_angle += 2;
    s_wave_angle += 3;
}

/**
 * @brief Updates and renders Mode-7 Style Infinite Scaling Forest.
 */
static void update_infinite_forest(void)
{
    int count = 0;

    for (int i = 0; i < NUM_FOREST_TREES; i++)
    {
        s_trees[i].z -= s_forest_speed;
        if (s_trees[i].z <= 14)
        {
            s_trees[i].z += 280;
            s_trees[i].x = ((i * 59 + (s_frame_count * 17)) % 380) - 190;
        }

        s16 rel_x = s_trees[i].x - s_cam_x;
        s16 rel_z = s_trees[i].z;

        s16 screen_x = 160 + (rel_x * 130) / rel_z;
        s16 screen_y = 108 + (58 * 120) / rel_z;

        if (screen_x >= -32 && screen_x < 330 && screen_y >= 90 && screen_y < 224)
        {
            u8 size; u16 tile; u8 pal; s16 off_x; s16 off_y;
            if (rel_z > 210)       { size = SPRITE_SIZE(1, 1); tile = TILE_TREE_8;  pal = PAL0; off_x = 4;  off_y = 8;  }
            else if (rel_z > 140)  { size = SPRITE_SIZE(2, 2); tile = TILE_TREE_16; pal = PAL1; off_x = 8;  off_y = 16; }
            else if (rel_z > 70)   { size = SPRITE_SIZE(3, 3); tile = TILE_TREE_24; pal = PAL2; off_x = 12; off_y = 24; }
            else                   { size = SPRITE_SIZE(4, 4); tile = TILE_TREE_32; pal = PAL3; off_x = 16; off_y = 32; }

            s_sorted_sprites[count].x = screen_x - off_x;
            s_sorted_sprites[count].y = screen_y - off_y;
            s_sorted_sprites[count].z = -rel_z; /* Smaller rel_z = closer to camera */
            s_sorted_sprites[count].size = size;
            s_sorted_sprites[count].attr = TILE_ATTR_FULL(pal, 0, 0, 0, tile);
            count++;
        }
    }

    /* Insertion sort: nearest (least rel_z / greatest -rel_z) first in Slot 0 */
    for (int i = 1; i < count; i++)
    {
        RenderSprite key = s_sorted_sprites[i];
        int j = i - 1;
        while (j >= 0 && s_sorted_sprites[j].z < key.z)
        {
            s_sorted_sprites[j + 1] = s_sorted_sprites[j];
            j--;
        }
        s_sorted_sprites[j + 1] = key;
    }

    /* Dispatch to VDP Sprite Cache */
    for (int i = 0; i < count; i++)
    {
        u8 link = (i == count - 1) ? 0 : (i + 1);
        VDP_setSpriteFull(i, s_sorted_sprites[i].x, s_sorted_sprites[i].y,
                          s_sorted_sprites[i].size, s_sorted_sprites[i].attr, link);
    }
    VDP_updateSprites(count, DMA_QUEUE);

    /* Perspective ground scrolling on Plane A */
    s_ground_scroll += s_forest_speed;
    VDP_setVerticalScroll(BG_A, s_ground_scroll);
    VDP_setHorizontalScroll(BG_A, -s_cam_x / 2);
}

int main(bool hardReset)
{
    (void)hardReset;

    JOY_init();
    init_vdp();
    update_hud();

    u16 prev_joy = 0;

    while (TRUE)
    {
        u16 joy = JOY_readJoypad(JOY_1);
        u16 pressed = joy & ~prev_joy;
        prev_joy = joy;

        /* START: Seamlessly toggle between Demo Modes */
        if (pressed & BUTTON_START)
        {
            switch_mode();
        }

        if (s_demo_mode == DEMO_MODE_DNA)
        {
            if (joy & BUTTON_LEFT)  s_dna_angle -= 3;
            if (joy & BUTTON_RIGHT) s_dna_angle += 3;
            if (joy & BUTTON_UP)    { if (s_dna_radius < 80) s_dna_radius++; }
            if (joy & BUTTON_DOWN)  { if (s_dna_radius > 20) s_dna_radius--; }

            if (pressed & BUTTON_A) s_wave_enabled = !s_wave_enabled;
            if (pressed & BUTTON_B) s_dna_radius = (s_dna_radius >= 65) ? 35 : (s_dna_radius + 10);

            update_dna_helix();
        }
        else
        {
            if (joy & BUTTON_LEFT)  s_cam_x -= 3;
            if (joy & BUTTON_RIGHT) s_cam_x += 3;
            if (joy & BUTTON_UP)    { if (s_forest_speed < 12) s_forest_speed++; }
            if (joy & BUTTON_DOWN)  { if (s_forest_speed > 2)  s_forest_speed--; }

            if (joy & BUTTON_A) s_forest_speed = 10; /* Turbo */
            if (joy & BUTTON_B) s_forest_speed = 3;  /* Brake */

            update_infinite_forest();
        }

        s_frame_count++;
        SYS_doVBlankProcess();
    }

    return 0;
}
