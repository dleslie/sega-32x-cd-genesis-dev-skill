/**
 * @file star_raster.h
 * @brief High-Speed Flat-Shaded Triangle Rasterizer and Lighting Pipeline for SH-2.
 *
 * Implements 2D backface culling, 3D face normal calculation, directional light
 * shading (single light source not in view), and 32-bit word scanline filling
 * directly into the 32X 15bpp direct color framebuffer.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _STAR_RASTER_H_
#define _STAR_RASTER_H_

#include "starfighter.h"

/* Fixed directional sun light vector (upper-left, slightly forward) */
#define LIGHT_DIR_X -50
#define LIGHT_DIR_Y -90
#define LIGHT_DIR_Z -70
#define LIGHT_DIR_LEN 124

/**
 * @brief Fast integer square root using bitwise iteration.
 */
static inline uint32_t fast_int_sqrt(uint32_t val)
{
    uint32_t res = 0;
    uint32_t bit = 1u << 30;
    while (bit > val)
    {
        bit >>= 2;
    }
    while (bit != 0)
    {
        if (val >= res + bit)
        {
            val -= res + bit;
            res = (res >> 1) + bit;
        }
        else
        {
            res >>= 1;
        }
        bit >>= 2;
    }
    return res;
}

/**
 * @brief Computes 15bpp flat-shaded color given 3D face normal and base material.
 */
static inline uint16_t compute_flat_shade(int32_t nx, int32_t ny, int32_t nz, StarMaterialColor mat)
{
    int32_t dot = nx * LIGHT_DIR_X + ny * LIGHT_DIR_Y + nz * LIGHT_DIR_Z;
    uint32_t n_len = fast_int_sqrt((uint32_t)(nx * nx + ny * ny + nz * nz));
    if (n_len == 0)
    {
        n_len = 1;
    }

    /* Cosine angle scaled to 0..256 */
    int32_t cos_val = (dot * 256) / (int32_t)(n_len * LIGHT_DIR_LEN);
    if (cos_val < 0)
    {
        cos_val = 0;
    }
    else if (cos_val > 256)
    {
        cos_val = 256;
    }

    /* Ambient base (28%) + Diffuse directional contribution (72%) */
    int32_t intensity = 9 + (23 * cos_val) / 256;
    if (intensity > 31)
    {
        intensity = 31;
    }

    uint32_t r = (mat.r * intensity) >> 5;
    uint32_t g = (mat.g * intensity) >> 5;
    uint32_t b = (mat.b * intensity) >> 5;

    return (uint16_t)(0x8000 | (b << 10) | (g << 5) | r);
}

/**
 * @brief Rasterizes a solid flat-shaded triangle into 32X Direct Color DRAM.
 */
static inline void draw_flat_triangle_fast(const StarVertex2D *p0, const StarVertex2D *p1, const StarVertex2D *p2,
                                           uint16_t color)
{
    /* Sort vertices vertically: v0 <= v1 <= v2 */
    const StarVertex2D *v0 = p0, *v1 = p1, *v2 = p2;
    if (v0->sy > v1->sy)
    {
        const StarVertex2D *tmp = v0; v0 = v1; v1 = tmp;
    }
    if (v1->sy > v2->sy)
    {
        const StarVertex2D *tmp = v1; v1 = v2; v2 = tmp;
    }
    if (v0->sy > v1->sy)
    {
        const StarVertex2D *tmp = v0; v0 = v1; v1 = tmp;
    }

    int32_t y0 = v0->sy, y1 = v1->sy, y2 = v2->sy;
    if (y0 == y2 || y2 < 0 || y0 >= FB_HEIGHT)
    {
        return;
    }

    /* Active backbuffer at 0x24000000 */
    volatile uint16_t *fb = (volatile uint16_t *)0x24000000;
    uint32_t color32 = ((uint32_t)color << 16) | color;

    int32_t dx02 = ((int32_t)(v2->sx - v0->sx) << 16) / (y2 - y0);

    /* Upper half (y0 to y1) */
    if (y1 > y0)
    {
        int32_t dx01 = ((int32_t)(v1->sx - v0->sx) << 16) / (y1 - y0);
        int32_t sy_start = (y0 < 0) ? 0 : y0;
        int32_t sy_end = (y1 >= FB_HEIGHT) ? FB_HEIGHT : y1;

        int32_t offset = sy_start - y0;
        int32_t cur_xa = ((int32_t)v0->sx << 16) + offset * dx02;
        int32_t cur_xb = ((int32_t)v0->sx << 16) + offset * dx01;

        for (int y = sy_start; y < sy_end; y++)
        {
            int32_t x_start = cur_xa >> 16, x_end = cur_xb >> 16;
            if (x_start > x_end)
            {
                int32_t t = x_start; x_start = x_end; x_end = t;
            }

            int32_t px_start = (x_start < 0) ? 0 : x_start;
            int32_t px_end = (x_end >= FB_WIDTH) ? FB_WIDTH : x_end;

            if (px_start < px_end)
            {
                int32_t line_base = 256 + (y * 320);
                int x = px_start;
                if ((x & 1) && x < px_end)
                {
                    fb[line_base + x] = color;
                    x++;
                }
                volatile uint32_t *fb32 = (volatile uint32_t *)(fb + line_base + x);
                int count = (px_end - x) >> 1;
                for (int i = 0; i < count; i++)
                {
                    *fb32++ = color32;
                }
                x += count << 1;
                if (x < px_end)
                {
                    fb[line_base + x] = color;
                }
            }

            cur_xa += dx02;
            cur_xb += dx01;
        }
    }

    /* Lower half (y1 to y2) */
    if (y2 > y1)
    {
        int32_t dx12 = ((int32_t)(v2->sx - v1->sx) << 16) / (y2 - y1);
        int32_t sy_start = (y1 < 0) ? 0 : y1;
        int32_t sy_end = (y2 >= FB_HEIGHT) ? FB_HEIGHT : y2;

        int32_t offset02 = sy_start - y0;
        int32_t offset12 = sy_start - y1;
        int32_t cur_xa = ((int32_t)v0->sx << 16) + offset02 * dx02;
        int32_t cur_xb = ((int32_t)v1->sx << 16) + offset12 * dx12;

        for (int y = sy_start; y < sy_end; y++)
        {
            int32_t x_start = cur_xa >> 16, x_end = cur_xb >> 16;
            if (x_start > x_end)
            {
                int32_t t = x_start; x_start = x_end; x_end = t;
            }

            int32_t px_start = (x_start < 0) ? 0 : x_start;
            int32_t px_end = (x_end >= FB_WIDTH) ? FB_WIDTH : x_end;

            if (px_start < px_end)
            {
                int32_t line_base = 256 + (y * 320);
                int x = px_start;
                if ((x & 1) && x < px_end)
                {
                    fb[line_base + x] = color;
                    x++;
                }
                volatile uint32_t *fb32 = (volatile uint32_t *)(fb + line_base + x);
                int count = (px_end - x) >> 1;
                for (int i = 0; i < count; i++)
                {
                    *fb32++ = color32;
                }
                x += count << 1;
                if (x < px_end)
                {
                    fb[line_base + x] = color;
                }
            }

            cur_xa += dx02;
            cur_xb += dx12;
        }
    }
}

/**
 * @brief Evaluates 2D backface culling, computes 3D lighting, and rasterizes a triangle.
 */
static inline void render_triangle_flat(const StarTriangle *tri, const StarSharedState *shared)
{
    const StarVertex2D *p0 = &shared->projected[tri->v0];
    const StarVertex2D *p1 = &shared->projected[tri->v1];
    const StarVertex2D *p2 = &shared->projected[tri->v2];

    /* Backface culling: evaluate 2D screen cross-product normal */
    int32_t normal = (int32_t)(p1->sx - p0->sx) * (int32_t)(p2->sy - p0->sy) -
                     (int32_t)(p1->sy - p0->sy) * (int32_t)(p2->sx - p0->sx);
    if (normal <= 0)
    {
        return;
    }

    /* 3D Face Normal in camera view space */
    const StarVertexTransformed *t0 = &shared->transformed[tri->v0];
    const StarVertexTransformed *t1 = &shared->transformed[tri->v1];
    const StarVertexTransformed *t2 = &shared->transformed[tri->v2];

    int32_t e1x = t1->fx - t0->fx, e1y = t1->fy - t0->fy, e1z = t1->fz - t0->fz;
    int32_t e2x = t2->fx - t0->fx, e2y = t2->fy - t0->fy, e2z = t2->fz - t0->fz;

    int32_t nx = e1y * e2z - e1z * e2y;
    int32_t ny = e1z * e2x - e1x * e2z;
    int32_t nz = e1x * e2y - e1y * e2x;

    uint16_t color = compute_flat_shade(nx, ny, nz, tri->material);
    draw_flat_triangle_fast(p0, p1, p2, color);
}

#endif /* _STAR_RASTER_H_ */
