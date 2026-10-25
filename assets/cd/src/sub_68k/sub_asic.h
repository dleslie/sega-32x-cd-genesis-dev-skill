/**
 * @file sub_asic.h
 * @brief Sega CD Graphics ASIC Hardware Co-Processor Interface.
 *
 * Provides hardware register definitions, trace table structures, and
 * affine transformation control routines for the Sega CD graphics ASIC.
 *
 * Copyright (c) 2026 Dan Leslie
 * SPDX-License-Identifier: MIT
 */

#ifndef SUB_ASIC_H
#define SUB_ASIC_H

#include <stdint.h>
#include <stdbool.h>

/* --- Sub-CPU ASIC Hardware Register Aperture (0xFF8058..0xFF8066) --- */

/**
 * Stamp Size & Status Register (0xFF8058):
 * - Bit 15 (GRON): Graphics Operation On (1 = ASIC busy rendering, 0 = idle).
 * - Bit 2  (SMS):  Stamp Map Size (0 = 256x256 pixel map, 1 = 4096x4096 pixel map).
 * - Bit 1  (STS):  Stamp Size (0 = 16x16 pixel stamps, 1 = 32x32 pixel stamps).
 * - Bit 0  (RPT):  Repeat Mode (0 = clamp/blank out of bounds, 1 = wrap map).
 */
#define SUB_REG_ASIC_STAMP_SIZE   (*(volatile uint16_t *)0xFF8058)

/** Stamp Map Base Address: Word RAM offset divided by 4 */
#define SUB_REG_ASIC_MAP_BASE     (*(volatile uint16_t *)0xFF805A)

/** Image Buffer Vertical Cell Size: (screen_height_pixels / 8) - 1 */
#define SUB_REG_ASIC_VCELL_SIZE   (*(volatile uint16_t *)0xFF805C)

/** Image Buffer Start Address: Word RAM destination offset divided by 4 */
#define SUB_REG_ASIC_IMG_START    (*(volatile uint16_t *)0xFF805E)

/** Image Buffer Offset: Bits 0-2 = horizontal offset, Bits 3-5 = vertical offset */
#define SUB_REG_ASIC_IMG_OFFSET   (*(volatile uint16_t *)0xFF8060)

/** Image Buffer Horizontal Pixel Size (Screen Width) */
#define SUB_REG_ASIC_H_PIXELS     (*(volatile uint16_t *)0xFF8062)

/** Image Buffer Vertical Pixel Size (Screen Height; decremented to 0 during execution) */
#define SUB_REG_ASIC_V_PIXELS     (*(volatile uint16_t *)0xFF8064)

/** Trace Table Base Address: Word RAM offset divided by 4; writing triggers operation! */
#define SUB_REG_ASIC_TRACE_BASE   (*(volatile uint16_t *)0xFF8066)

/* --- ASIC Control Register Bitmasks --- */
#define ASIC_FLAG_BUSY_GRON       0x8000
#define ASIC_MAP_SIZE_256         0x0000
#define ASIC_MAP_SIZE_4096        0x0004
#define ASIC_STAMP_SIZE_16        0x0000
#define ASIC_STAMP_SIZE_32        0x0002
#define ASIC_REPEAT_WRAP          0x0001

/* --- Target Image Buffer Configuration --- */
#define ASIC_SCREEN_WIDTH         160
#define ASIC_SCREEN_HEIGHT        128
#define ASIC_VCELL_COUNT          ((ASIC_SCREEN_HEIGHT / 8) - 1)

/* --- Word RAM Memory Map Layout (1M Mode Bank: 0x0C0000..0x0DFFFF) --- */
#define WRAM_1M_BASE              0x0C0000

#define WRAM_STAMP_TILES_OFFSET   0x0000   /* 0x0000..0x01FF: Stamp patterns (Stamp 0 blank) */
#define WRAM_STAMP_MAP_OFFSET     0x0200   /* 0x0200..0x03FF: Stamp map (16x16 stamps = 512B) */
#define WRAM_TRACE_TABLE_OFFSET   0x0600   /* 0x0600..0x09FF: Scanline trace vector array */
#define WRAM_IMAGE_BUFFER_OFFSET  0x1000   /* 0x1000..0x37FF: 160x128 4bpp tile output */

/**
 * @brief Scanline Trace Vector Table Entry.
 * The ASIC reads one entry per horizontal raster line of the image buffer.
 */
typedef struct {
    int16_t start_x;  /**< Initial horizontal sampling coordinate in 13.3 fixed-point */
    int16_t start_y;  /**< Initial vertical sampling coordinate in 13.3 fixed-point */
    int16_t delta_x;  /**< Horizontal step vector per pixel in signed 5.11 fixed-point */
    int16_t delta_y;  /**< Cross step vector per pixel in signed 5.11 fixed-point */
} __attribute__((packed)) SubAsicTraceEntry;

/**
 * @brief Evaluates fixed-point sine via 256-step table (5.11 format: 2048 = 1.0).
 */
int16_t sub_sin(uint8_t angle);

/**
 * @brief Evaluates fixed-point cosine via 256-step table (5.11 format: 2048 = 1.0).
 */
int16_t sub_cos(uint8_t angle);

/**
 * @brief Initializes ASIC stamp patterns, stamp map, and coordinate tables in Word RAM.
 */
void sub_asic_init(void);

/**
 * @brief Computes affine transformation vectors and triggers the ASIC hardware rasterizer.
 *
 * @param angle_idx 8-bit rotation angle (0..255 representing 0..360 degrees).
 * @param scale_q8  8.8 fixed-point scale factor (256 = 1.0x normal size).
 */
void sub_asic_render_transform(uint8_t angle_idx, uint16_t scale_q8);

/**
 * @brief Polls the ASIC status register until the active graphics operation finishes.
 */
void sub_asic_wait_idle(void);

#endif /* SUB_ASIC_H */
