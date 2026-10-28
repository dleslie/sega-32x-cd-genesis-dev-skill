/**
 * @file mars_master.c
 * @brief Sega 32X Master SH-2 Main Loop (Tower of Power 3D Starfighter).
 *
 * Coordinates 3D perspective transformation, 3D starfield, dual-core parallel
 * polygon rasterization with Slave SH-2, and 32X VDP direct-color double-buffering.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "sh2_ipc.h"
#include "sh2_test.h"
#include "starfighter.h"
#include "star_raster.h"

/* 256-Entry Fixed-Point Sine Table (5.11 format: 2048 = 1.0) */
const int16_t g_sin_lut[256] = {
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

/* 32x32 15bpp BGR Texture Map */
uint16_t g_ship_texture[1024];

/* Shared Inter-Core State located in SDRAM */
StarSharedState g_shared_state __attribute__((section(".data")));

static uint16_t s_frame_count = 0;
static uint8_t  s_angle_pitch = 18;
static uint8_t  s_angle_yaw   = 0;
static uint8_t  s_angle_roll  = 6;

static StarParticle s_stars[NUM_STARS];

/**
 * @brief Generates high-contrast 32x32 15bpp texture map in RAM.
 */
static void init_texture_map(void)
{
    for (int y = 0; y < 32; y++)
    {
        for (int x = 0; x < 32; x++)
        {
            /* Base metallic titanium grey */
            uint16_t c = 0x8000 | (18 << 10) | (20 << 5) | 22;

            /* Glass cockpit canopy (U: 10..22, V: 2..16) */
            if (x >= 10 && x <= 22 && y >= 2 && y <= 16)
            {
                int dx = x - 16, dy = y - 8;
                if ((dx * dx + dy * dy) <= 32)
                {
                    /* Radiant cyan tinted cockpit glass */
                    c = 0x8000 | (28 << 10) | (26 << 5) | 6;
                    if (x == 14 && y <= 9)
                    {
                        c = 0xFFFF; /* Specular sun reflection highlight */
                    }
                }
            }
            /* Bold crimson red squadron insignia stripes */
            else if ((x + y >= 26 && x + y <= 30) || (x - y >= 8 && x - y <= 12))
            {
                c = 0x8000 | (3 << 10) | (3 << 5) | 28;
            }
            /* Dark armor panel seams and rivets */
            else if (x == 0 || x == 31 || y == 0 || y == 31 || x == 16)
            {
                c = 0x8000 | (6 << 10) | (6 << 5) | 7;
            }

            g_ship_texture[(y << 5) | x] = c;
        }
    }
}

/**
 * @brief Initializes 3D starfield particles.
 */
static void init_starfield(void)
{
    for (int i = 0; i < NUM_STARS; i++)
    {
        s_stars[i].x = (int16_t)(((i * 47) % 300) - 150);
        s_stars[i].y = (int16_t)(((i * 73) % 200) - 100);
        s_stars[i].z = (int16_t)(((i * 31) % 220) + 20);
        s_stars[i].speed = (int16_t)((i % 3) + 1);
    }
}

/**
 * @brief Configures 32X VDP direct color mode.
 */
void mars_vdp_init(void)
{
    /* Mode 2: 320x224 Direct Color Mode (15bpp BGR) */
    MARS_VDP_MODE_REG = 0x0002;
    MARS_VDP_FS_REG   = 0x0000;
}

/**
 * @brief Swaps active 32X framebuffer and synchronizes with VBlank.
 */
void mars_vdp_flip(void)
{
    MARS_VDP_FS_REG ^= 0x0001;
    while ((MARS_VDP_FS_REG & 0x0002) != 0)
    {
        __asm__ volatile ("nop");
    }
}

/**
 * @brief Master SH-2 primary execution loop.
 */
void mars_master_main(void)
{
    /* Run multi-core test verification and hardware handshake */
    g_test_state = sh2_test_get_state();
    sh2_test_init_state(g_test_state);
    sh2_test_master_run(g_test_state);

    init_texture_map();
    init_starfield();
    mars_vdp_init();

    StarSharedState *shared = (StarSharedState *)SH2_UNCACHED(&g_shared_state);

    while (1)
    {
        /* 1. Read input telemetry from Genesis Main 68000 */
        uint16_t pad = MARS_COMM5_REG;

        /* D-Pad interactive rotation control or continuous gentle automatic rotation */
        if (pad & 0x0004) /* Left */
        {
            s_angle_yaw = (uint8_t)(s_angle_yaw - 3);
        }
        else if (pad & 0x0008) /* Right */
        {
            s_angle_yaw = (uint8_t)(s_angle_yaw + 3);
        }
        else
        {
            s_angle_yaw = (uint8_t)(s_angle_yaw + 1); /* Continuous slow yaw spin */
        }

        if (pad & 0x0001) /* Up */
        {
            s_angle_pitch = (uint8_t)(s_angle_pitch - 2);
        }
        else if (pad & 0x0002) /* Down */
        {
            s_angle_pitch = (uint8_t)(s_angle_pitch + 2);
        }
        else
        {
            s_angle_roll = (uint8_t)(s_angle_roll + 1); /* Subtle space wobble */
        }

        /* 2. 3D Fixed-Point Matrix Transformation & Perspective Projection */
        int32_t sin_y = star_sin(s_angle_yaw),   cos_y = star_cos(s_angle_yaw);
        int32_t sin_p = star_sin(s_angle_pitch), cos_p = star_cos(s_angle_pitch);
        int32_t sin_r = star_sin(s_angle_roll),  cos_r = star_cos(s_angle_roll);

        for (int i = 0; i < 16; i++)
        {
            int32_t vx = s_ship_vertices[i].x;
            int32_t vy = s_ship_vertices[i].y;
            int32_t vz = s_ship_vertices[i].z;

            /* Yaw rotation around Y-axis */
            int32_t rx  = (vx * cos_y + vz * sin_y) >> 11;
            int32_t rz1 = (-vx * sin_y + vz * cos_y) >> 11;

            /* Pitch rotation around X-axis */
            int32_t ry  = (vy * cos_p - rz1 * sin_p) >> 11;
            int32_t rz2 = (vy * sin_p + rz1 * cos_p) >> 11;

            /* Roll rotation around Z-axis */
            int32_t fx = (rx * cos_r - ry * sin_r) >> 11;
            int32_t fy = (rx * sin_r + ry * cos_r) >> 11;
            int32_t fz = rz2 + 180; /* Camera distance */

            if (fz < 20)
            {
                fz = 20;
            }

            /* Perspective projection to 320x200 screen (FOV = 220) */
            shared->projected[i].sx = (int16_t)((fx * 220) / fz + (FB_WIDTH / 2));
            shared->projected[i].sy = (int16_t)((fy * 220) / fz + (FB_HEIGHT / 2));
            shared->projected[i].sz = (int16_t)fz;
        }

        /* 3. Prepare Framebuffer at 0x24000000 */
        volatile uint16_t *fb = (volatile uint16_t *)0x24000000;

        /* Write line table for currently active backbuffer bank */
        for (int y = 0; y < 224; y++)
        {
            int line_idx = (y < FB_HEIGHT) ? y : (FB_HEIGHT - 1);
            fb[y] = (uint16_t)(256 + (line_idx * 320));
        }

        /* Clear to 0x0000 (transparent: Genesis VDP starfield shines through behind) */
        for (int y = 0; y < FB_HEIGHT; y++)
        {
            int line_start = 256 + (y * 320);
            for (int x = 0; x < FB_WIDTH; x += 4)
            {
                fb[line_start + x + 0] = 0x0000;
                fb[line_start + x + 1] = 0x0000;
                fb[line_start + x + 2] = 0x0000;
                fb[line_start + x + 3] = 0x0000;
            }
        }

        /* 4. Update and render 3D starfield particles */
        for (int i = 0; i < NUM_STARS; i++)
        {
            s_stars[i].z -= s_stars[i].speed;
            if (s_stars[i].z <= 10)
            {
                s_stars[i].z = 220;
                s_stars[i].x = (int16_t)(((i * 59) % 300) - 150);
                s_stars[i].y = (int16_t)(((i * 83) % 200) - 100);
            }

            int32_t px = (s_stars[i].x * 200) / s_stars[i].z + (FB_WIDTH / 2);
            int32_t py = (s_stars[i].y * 200) / s_stars[i].z + (FB_HEIGHT / 2);

            if (px >= 0 && px < FB_WIDTH && py >= 0 && py < FB_HEIGHT)
            {
                uint16_t star_color = (s_stars[i].z < 70) ? 0xFFFF :
                                     ((s_stars[i].z < 130) ? (0x8000 | (24 << 10) | (24 << 5) | 24) :
                                                             (0x8000 | (12 << 10) | (12 << 5) | 12));
                fb[256 + (py * 320) + px] = star_color;
            }
        }

        /* 5. Signal Slave SH-2 to concurrently render Batch 2 polygons */
        shared->frame_id = s_frame_count;
        MARS_COMM2_REG = s_frame_count;

        /* 6. Master SH-2 Renders Batch 1 (Fuselage, Canopy, Nose, Spine) */
        for (int i = 0; i < 10; i++)
        {
            draw_textured_triangle_fast(&shared->projected[s_batch1_triangles[i].v0],
                                        &shared->projected[s_batch1_triangles[i].v1],
                                        &shared->projected[s_batch1_triangles[i].v2],
                                        s_batch1_triangles[i].t0,
                                        s_batch1_triangles[i].t1,
                                        s_batch1_triangles[i].t2,
                                        g_ship_texture);
        }

        /* 7. Await Slave SH-2 completion of Batch 2 (Wings, Fins, Keel, Engines) */
        uint32_t slave_timeout = 100000;
        while ((MARS_COMM3_REG != s_frame_count) && --slave_timeout)
        {
            __asm__ volatile ("nop");
        }

        /* 7. Draw pulsating engine thruster flame at rear exhaust */
        int32_t ex = shared->projected[15].sx;
        int32_t ey = shared->projected[15].sy;
        int32_t ez = shared->projected[15].sz;

        if (ex >= 2 && ex < (FB_WIDTH - 2) && ey >= 2 && ey < (FB_HEIGHT - 2) && ez > 30)
        {
            uint16_t flame_color = ((s_frame_count & 4) == 0) ? (0x8000 | (31 << 10) | (28 << 5) | 4) :
                                                                (0x8000 | (28 << 10) | (16 << 5) | 2);
            int base_line = 256 + (ey * 320);
            fb[base_line + ex] = 0xFFFF; /* White hot core */
            fb[base_line + ex - 1] = flame_color;
            fb[base_line + ex + 1] = flame_color;
            fb[base_line - 320 + ex] = flame_color;
            fb[base_line + 320 + ex] = flame_color;
        }

        s_frame_count++;
        MARS_COMM7_REG = s_frame_count; /* Report frame counter to Genesis */

        /* Flip double-buffered framebuffer at VBlank */
        mars_vdp_flip();
    }
}
