/**
 * @file star_raster.h
 * @brief High-Speed Fixed-Point Scanline Affine Triangle Rasterizer for SH-2.
 *
 * Implements backface culling, bounding-box screen clipping, and fixed-point
 * UV coordinate stepping directly into the 32X 15bpp direct color framebuffer.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _STAR_RASTER_H_
#define _STAR_RASTER_H_

#include "starfighter.h"

/**
 * @brief Rasterizes a texture-mapped triangle into 32X Direct Color DRAM.
 *
 * @param p0 Projected vertex 0 screen coordinates.
 * @param p1 Projected vertex 1 screen coordinates.
 * @param p2 Projected vertex 2 screen coordinates.
 * @param t0 Vertex 0 UV coordinates.
 * @param t1 Vertex 1 UV coordinates.
 * @param t2 Vertex 2 UV coordinates.
 * @param tex Pointer to 32x32 15bpp texture map array (1024 words).
 */
static inline void draw_textured_triangle_fast(const StarVertex2D *p0, const StarVertex2D *p1, const StarVertex2D *p2,
                                              StarTexCoord t0, StarTexCoord t1, StarTexCoord t2,
                                              const uint16_t *tex)
{
    /* Backface culling: evaluate cross product normal */
    int32_t normal = (int32_t)(p1->sx - p0->sx) * (int32_t)(p2->sy - p0->sy) -
                     (int32_t)(p1->sy - p0->sy) * (int32_t)(p2->sx - p0->sx);
    if (normal <= 0)
    {
        return;
    }

    /* Sort vertices vertically: v0 <= v1 <= v2 */
    const StarVertex2D *v0 = p0, *v1 = p1, *v2 = p2;
    StarTexCoord uv0 = t0, uv1 = t1, uv2 = t2;
    if (v0->sy > v1->sy)
    {
        const StarVertex2D *tmp = v0; v0 = v1; v1 = tmp;
        StarTexCoord tmp_uv = uv0; uv0 = uv1; uv1 = tmp_uv;
    }
    if (v1->sy > v2->sy)
    {
        const StarVertex2D *tmp = v1; v1 = v2; v2 = tmp;
        StarTexCoord tmp_uv = uv1; uv1 = uv2; uv2 = tmp_uv;
    }
    if (v0->sy > v1->sy)
    {
        const StarVertex2D *tmp = v0; v0 = v1; v1 = tmp;
        StarTexCoord tmp_uv = uv0; uv0 = uv1; uv1 = tmp_uv;
    }

    int32_t y0 = v0->sy, y1 = v1->sy, y2 = v2->sy;
    if (y0 == y2 || y2 < 0 || y0 >= FB_HEIGHT)
    {
        return;
    }

    /* Active backbuffer at 0x24000000 */
    volatile uint16_t *fb = (volatile uint16_t *)0x24000000;

    int32_t dx02 = ((int32_t)(v2->sx - v0->sx) << 16) / (y2 - y0);
    int32_t du02 = ((int32_t)(uv2.u - uv0.u) << 16) / (y2 - y0);
    int32_t dv02 = ((int32_t)(uv2.v - uv0.v) << 16) / (y2 - y0);

    /* Upper half (y0 to y1) */
    if (y1 > y0)
    {
        int32_t dx01 = ((int32_t)(v1->sx - v0->sx) << 16) / (y1 - y0);
        int32_t du01 = ((int32_t)(uv1.u - uv0.u) << 16) / (y1 - y0);
        int32_t dv01 = ((int32_t)(uv1.v - uv0.v) << 16) / (y1 - y0);

        int32_t sy_start = (y0 < 0) ? 0 : y0;
        int32_t sy_end = (y1 >= FB_HEIGHT) ? FB_HEIGHT : y1;

        int32_t offset = sy_start - y0;
        int32_t cur_xa = ((int32_t)v0->sx << 16) + offset * dx02;
        int32_t cur_xb = ((int32_t)v0->sx << 16) + offset * dx01;
        int32_t cur_ua = ((int32_t)uv0.u << 16) + offset * du02;
        int32_t cur_ub = ((int32_t)uv0.u << 16) + offset * du01;
        int32_t cur_va = ((int32_t)uv0.v << 16) + offset * dv02;
        int32_t cur_vb = ((int32_t)uv0.v << 16) + offset * dv01;

        for (int y = sy_start; y < sy_end; y++)
        {
            int32_t x_start = cur_xa >> 16, x_end = cur_xb >> 16;
            int32_t u_start = cur_ua, u_end = cur_ub;
            int32_t v_start = cur_va, v_end = cur_vb;

            if (x_start > x_end)
            {
                int32_t t = x_start; x_start = x_end; x_end = t;
                t = u_start; u_start = u_end; u_end = t;
                t = v_start; v_start = v_end; v_end = t;
            }

            int32_t span = x_end - x_start;
            if (span > 0)
            {
                int32_t du = (u_end - u_start) / span;
                int32_t dv = (v_end - v_start) / span;
                int32_t u = u_start, v = v_start;

                int32_t px_start = (x_start < 0) ? 0 : x_start;
                int32_t px_end = (x_end >= FB_WIDTH) ? FB_WIDTH : x_end;

                if (px_start > x_start)
                {
                    int32_t off = px_start - x_start;
                    u += off * du;
                    v += off * dv;
                }

                int32_t line_base = 256 + (y * 320);
                for (int x = px_start; x < px_end; x++)
                {
                    uint8_t tu = (uint8_t)((u >> 16) & 31);
                    uint8_t tv = (uint8_t)((v >> 16) & 31);
                    fb[line_base + x] = tex[(tv << 5) | tu];
                    u += du;
                    v += dv;
                }
            }

            cur_xa += dx02; cur_xb += dx01;
            cur_ua += du02; cur_ub += du01;
            cur_va += dv02; cur_vb += dv01;
        }
    }

    /* Lower half (y1 to y2) */
    if (y2 > y1)
    {
        int32_t dx12 = ((int32_t)(v2->sx - v1->sx) << 16) / (y2 - y1);
        int32_t du12 = ((int32_t)(uv2.u - uv1.u) << 16) / (y2 - y1);
        int32_t dv12 = ((int32_t)(uv2.v - uv1.v) << 16) / (y2 - y1);

        int32_t sy_start = (y1 < 0) ? 0 : y1;
        int32_t sy_end = (y2 >= FB_HEIGHT) ? FB_HEIGHT : y2;

        int32_t offset02 = sy_start - y0;
        int32_t offset12 = sy_start - y1;
        int32_t cur_xa = ((int32_t)v0->sx << 16) + offset02 * dx02;
        int32_t cur_xb = ((int32_t)v1->sx << 16) + offset12 * dx12;
        int32_t cur_ua = ((int32_t)uv0.u << 16) + offset02 * du02;
        int32_t cur_ub = ((int32_t)uv1.u << 16) + offset12 * du12;
        int32_t cur_va = ((int32_t)uv0.v << 16) + offset02 * dv02;
        int32_t cur_vb = ((int32_t)uv1.v << 16) + offset12 * dv12;

        for (int y = sy_start; y < sy_end; y++)
        {
            int32_t x_start = cur_xa >> 16, x_end = cur_xb >> 16;
            int32_t u_start = cur_ua, u_end = cur_ub;
            int32_t v_start = cur_va, v_end = cur_vb;

            if (x_start > x_end)
            {
                int32_t t = x_start; x_start = x_end; x_end = t;
                t = u_start; u_start = u_end; u_end = t;
                t = v_start; v_start = v_end; v_end = t;
            }

            int32_t span = x_end - x_start;
            if (span > 0)
            {
                int32_t du = (u_end - u_start) / span;
                int32_t dv = (v_end - v_start) / span;
                int32_t u = u_start, v = v_start;

                int32_t px_start = (x_start < 0) ? 0 : x_start;
                int32_t px_end = (x_end >= FB_WIDTH) ? FB_WIDTH : x_end;

                if (px_start > x_start)
                {
                    int32_t off = px_start - x_start;
                    u += off * du;
                    v += off * dv;
                }

                int32_t line_base = 256 + (y * 320);
                for (int x = px_start; x < px_end; x++)
                {
                    uint8_t tu = (uint8_t)((u >> 16) & 31);
                    uint8_t tv = (uint8_t)((v >> 16) & 31);
                    fb[line_base + x] = tex[(tv << 5) | tu];
                    u += du;
                    v += dv;
                }
            }

            cur_xa += dx02; cur_xb += dx12;
            cur_ua += du02; cur_ub += du12;
            cur_va += dv02; cur_vb += dv12;
        }
    }
}

#endif /* _STAR_RASTER_H_ */
