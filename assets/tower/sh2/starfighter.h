/**
 * @file starfighter.h
 * @brief 3D Starfighter Geometry, Materials, and Dual SH-2 Shared Pipeline.
 *
 * Defines the 3D Starfighter polygon mesh, material color properties,
 * and dual-core parallel workload division for Sega 32X Tower of Power demo.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _STARFIGHTER_H_
#define _STARFIGHTER_H_

#include <stdint.h>
#include <stdbool.h>

#define FB_WIDTH   320
#define FB_HEIGHT  200

/* Sine/Cosine 256-Entry LUT in 5.11 format (2048 = 1.0) */
extern const int16_t g_sin_lut[256];

static inline int16_t star_sin(uint8_t a)
{
    return g_sin_lut[a];
}

static inline int16_t star_cos(uint8_t a)
{
    return g_sin_lut[(uint8_t)(a + 64)];
}

/* 3D Model Vertex Coordinate */
typedef struct {
    int16_t x, y, z;
} StarVertex3D;

/* 3D Transformed Vertex (before perspective projection) */
typedef struct {
    int16_t fx, fy, fz;
} StarVertexTransformed;

/* 2D Projected Screen Coordinate */
typedef struct {
    int16_t sx, sy, sz;
} StarVertex2D;

/* Material Color in 5-bit RGB (0..31 per channel) */
typedef struct {
    uint8_t r, g, b;
} StarMaterialColor;

/* Flat-Shaded Polygon Structure */
typedef struct {
    uint8_t v0, v1, v2;
    StarMaterialColor material;
} StarTriangle;

/* Render Item Types for Back-to-Front Painter's Algorithm Sorting */
#define ITEM_TYPE_TRIANGLE 0
#define ITEM_TYPE_FLAME    1
#define ITEM_TYPE_STROBE_L 2
#define ITEM_TYPE_STROBE_R 3

/* Render Queue Item for Back-to-Front Painter's Algorithm Depth Sorting */
typedef struct {
    uint8_t item_type;
    uint8_t tri_idx;
    int16_t z_depth;
    uint16_t color;
} StarRenderItem;

/* 3D Star Particle */
typedef struct {
    int16_t x, y, z, speed;
} StarParticle;

#define NUM_STARS 64

/* Shared Inter-Core Frame Data in Uncached SDRAM */
typedef struct {
    StarVertex2D projected[16];
    StarVertexTransformed transformed[16];
    StarRenderItem sorted_queue[32];
    uint8_t queue_count;
    volatile uint16_t frame_id;
    volatile uint16_t slave_ack;
} StarSharedState;

/* Model Geometry: 16 Vertices */
static const StarVertex3D s_ship_vertices[16] = {
    {   0,   0,  65 }, /* 0: Nose cone forward tip */
    {   0, -16,  12 }, /* 1: Cockpit canopy apex */
    { -14,  -2,   5 }, /* 2: Left fuselage shoulder */
    {  14,  -2,   5 }, /* 3: Right fuselage shoulder */
    {   0,  -8, -35 }, /* 4: Dorsal spine rear */
    {   0,  10, -40 }, /* 5: Ventral keel rear */
    {   0,   8,  15 }, /* 6: Ventral keel front */
    { -16,   2, -40 }, /* 7: Left wing root */
    {  16,   2, -40 }, /* 8: Right wing root */
    { -65,   4, -30 }, /* 9: Left delta wingtip */
    {  65,   4, -30 }, /* 10: Right delta wingtip */
    { -22, -24, -40 }, /* 11: Left vertical stabilizer */
    {  22, -24, -40 }, /* 12: Right vertical stabilizer */
    {  -8,  -2, -44 }, /* 13: Left engine exhaust nozzle */
    {   8,  -2, -44 }, /* 14: Right engine exhaust nozzle */
    {   0,  -2, -48 }  /* 15: Engine flame center */
};

/* Unified Model Mesh: 24 Polygons (Including Watertight Rear Engine Quad) */
static const StarTriangle s_ship_triangles[24] = {
    /* Canopy / Cockpit */
    { 0, 1, 2, { 4, 24, 31 } }, /* 0: Canopy Glass Left (Cyan Glass) */
    { 0, 3, 1, { 4, 24, 31 } }, /* 1: Canopy Glass Right (Cyan Glass) */
    { 1, 4, 2, { 26, 27, 28 } }, /* 2: Cockpit Spine Left (Silver Titanium) */
    { 1, 3, 4, { 26, 27, 28 } }, /* 3: Cockpit Spine Right (Silver Titanium) */

    /* Dorsal Hull & Flanks */
    { 2, 4, 7, { 22, 22, 24 } }, /* 4: Dorsal Hull Left (Steel Alloy) */
    { 3, 8, 4, { 22, 22, 24 } }, /* 5: Dorsal Hull Right (Steel Alloy) */
    { 2, 7, 6, { 18, 18, 20 } }, /* 6: Fuselage Flank Left (Mid Armor Grey) */
    { 3, 6, 8, { 18, 18, 20 } }, /* 7: Fuselage Flank Right (Mid Armor Grey) */

    /* Nose & Belly */
    { 0, 2, 6, { 24, 24, 26 } }, /* 8: Nose Left Lower (Light Grey) */
    { 0, 6, 3, { 24, 24, 26 } }, /* 9: Nose Right Lower (Light Grey) */
    { 6, 7, 5, { 12, 12, 14 } }, /* 10: Ventral Belly Left (Dark Belly Grey) */
    { 6, 5, 8, { 12, 12, 14 } }, /* 11: Ventral Belly Right (Dark Belly Grey) */

    /* Delta Wings (double-sided) */
    { 2, 7, 9,  { 28,  4,  6 } }, /* 12: Left Delta Wing Upper (Crimson Squadron Stripe) */
    { 2, 9, 7,  { 18, 18, 20 } }, /* 13: Left Delta Wing Lower (Armor Grey) */
    { 3, 10, 8, { 28,  4,  6 } }, /* 14: Right Delta Wing Upper (Crimson Squadron Stripe) */
    { 3, 8, 10, { 18, 18, 20 } }, /* 15: Right Delta Wing Lower (Armor Grey) */

    /* Vertical Stabilizers (double-sided) */
    { 4, 11, 7, { 28,  3,  5 } }, /* 16: Left Vertical Fin Outer (Squadron Red) */
    { 4, 7, 11, { 20,  3,  4 } }, /* 17: Left Vertical Fin Inner (Dark Red) */
    { 4, 8, 12, { 20,  3,  4 } }, /* 18: Right Vertical Fin Inner (Dark Red) */
    { 4, 12, 8, { 28,  3,  5 } }, /* 19: Right Vertical Fin Outer (Squadron Red) */

    /* Engine Nozzles */
    { 7, 5, 13, {  7,  7,  9 } }, /* 20: Left Exhaust Nozzle (Dark Graphite) */
    { 8, 14, 5, {  7,  7,  9 } }, /* 21: Right Exhaust Nozzle (Dark Graphite) */

    /* Rear Engine Bulkhead (Watertight Rear Quad) */
    { 4, 5, 8,  {  8,  8, 10 } }, /* 22: Rear Bulkhead Starboard (Dark Ceramic Heat Shield) */
    { 4, 7, 5,  {  8,  8, 10 } }  /* 23: Rear Bulkhead Port (Dark Ceramic Heat Shield) */
};

#endif /* _STARFIGHTER_H_ */
