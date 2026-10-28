/**
 * @file starfighter.h
 * @brief 3D Starfighter Geometry, Texture Map, and Dual SH-2 Shared Pipeline.
 *
 * Defines the 3D Starfighter polygon mesh, 32x32 high-contrast texture map,
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

/* 2D Projected Screen Coordinate */
typedef struct {
    int16_t sx, sy, sz;
} StarVertex2D;

/* 2D Texture Coordinate (0..31) */
typedef struct {
    uint8_t u, v;
} StarTexCoord;

/* Textured Polygon Structure */
typedef struct {
    uint8_t v0, v1, v2;
    StarTexCoord t0, t1, t2;
    uint16_t tint;
} StarTriangle;

/* 3D Star Particle */
typedef struct {
    int16_t x, y, z, speed;
} StarParticle;

#define NUM_STARS 64

/* Shared Inter-Core Frame Data in Uncached SDRAM */
typedef struct {
    StarVertex2D projected[16];
    volatile uint16_t frame_id;
    volatile uint16_t slave_ack;
} StarSharedState;

/* Model Geometry: 16 Vertices */
static const StarVertex3D s_ship_vertices[16] = {
    {   0,   0,  65 }, /* 0: Nose cone forward tip */
    {   0, -16,  12 }, /* 1: Cockpit canopy apex */
    { -14,  -2,   5 }, /* 2: Left fuselage shoulder */
    {  14,  -2,   5 }, /* 3: Right fuselage shoulder */
    {   0,  -8, -40 }, /* 4: Dorsal spine rear */
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

/* Batch 1: Master SH-2 Polygons (Fuselage, Canopy, Nose, Spine, Keel) */
static const StarTriangle s_batch1_triangles[10] = {
    { 0, 1, 2,  {16,  0}, { 8, 16}, { 0, 31}, 0x9CE7 }, /* Canopy Glass Left */
    { 0, 3, 1,  {16,  0}, {31, 31}, {24, 16}, 0xBDEF }, /* Canopy Glass Right */
    { 1, 4, 2,  {16,  8}, {16, 31}, { 0, 16}, 0x8410 }, /* Cockpit Spine Left */
    { 1, 3, 4,  {16,  8}, {31, 16}, {16, 31}, 0x8C30 }, /* Cockpit Spine Right */
    { 0, 2, 6,  {16,  0}, { 0, 16}, {16, 24}, 0x94A5 }, /* Nose Left Lower */
    { 0, 6, 3,  {16,  0}, {16, 24}, {31, 16}, 0x94A5 }, /* Nose Right Lower */
    { 2, 4, 7,  { 0, 16}, {16, 31}, { 8, 31}, 0x8410 }, /* Dorsal Hull Left */
    { 3, 8, 4,  {31, 16}, {24, 31}, {16, 31}, 0x8C30 }, /* Dorsal Hull Right */
    { 6, 7, 5,  {16, 24}, { 8, 31}, {16, 31}, 0x738C }, /* Ventral Belly Left */
    { 6, 5, 8,  {16, 24}, {16, 31}, {24, 31}, 0x738C }  /* Ventral Belly Right */
};

/* Batch 2: Slave SH-2 Polygons (Double-Sided Wings & Vertical Fins, Keel, Engines) */
static const StarTriangle s_batch2_triangles[10] = {
    { 2, 7, 9,   { 0, 16}, { 8, 31}, { 0, 31}, 0xA514 }, /* Left Delta Wing Upper */
    { 2, 9, 7,   { 0, 16}, { 0, 31}, { 8, 31}, 0x94A5 }, /* Left Delta Wing Lower */
    { 3, 10, 8,  {31, 16}, {31, 31}, {24, 31}, 0xAD35 }, /* Right Delta Wing Upper */
    { 3, 8, 10,  {31, 16}, {24, 31}, {31, 31}, 0x94A5 }, /* Right Delta Wing Lower */
    { 4, 11, 7,  {16, 31}, { 8,  8}, { 8, 31}, 0xB904 }, /* Left Vertical Fin Left */
    { 4, 7, 11,  {16, 31}, { 8, 31}, { 8,  8}, 0xA8E3 }, /* Left Vertical Fin Right */
    { 4, 8, 12,  {16, 31}, {24, 31}, {24,  8}, 0xC125 }, /* Right Vertical Fin Left */
    { 4, 12, 8,  {16, 31}, {24,  8}, {24, 31}, 0xB104 }, /* Right Vertical Fin Right */
    { 7, 5, 13,  { 8, 31}, {16, 31}, {12, 31}, 0x6318 }, /* Left Exhaust Nozzle */
    { 8, 14, 5,  {24, 31}, {20, 31}, {16, 31}, 0x6318 }  /* Right Exhaust Nozzle */
};

#endif /* _STARFIGHTER_H_ */
