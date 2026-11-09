// SPDX-License-Identifier: MIT
#ifndef D32_VGM_H
#define D32_VGM_H

#include <stdint.h>
#include <stddef.h>

/*
 * Standard Video Game Music (VGM v1.50+) definitions for Sega Genesis / 32X.
 *
 * Used by host verification tests, toolchain validators, and the 68000
 * audio streaming driver for YM2612 (FM) and SN76489 (PSG).
 */

#define D32_VGM_MAGIC           0x206D6756  /* "Vgm " in little-endian / ASCII */
#define D32_VGM_VERSION_150     0x00000150
#define D32_VGM_VERSION_170     0x00000170

/* Standard Genesis clock frequencies */
#define D32_VGM_YM2612_CLOCK    7670453     /* NTSC YM2612: 7.670453 MHz */
#define D32_VGM_SN76489_CLOCK   3579545     /* NTSC PSG: 3.579545 MHz */

/* Standard VGM command opcodes */
#define D32_VGM_CMD_PSG         0x50        /* PSG (SN76489) write byte */
#define D32_VGM_CMD_YM2612_P0   0x52        /* YM2612 port 0 write (reg, data) */
#define D32_VGM_CMD_YM2612_P1   0x53        /* YM2612 port 1 write (reg, data) */
#define D32_VGM_CMD_WAIT        0x61        /* Wait N samples (16-bit word) */
#define D32_VGM_CMD_WAIT_60HZ   0x62        /* Wait 735 samples (1/60 s) */
#define D32_VGM_CMD_WAIT_50HZ   0x63        /* Wait 882 samples (1/50 s) */
#define D32_VGM_CMD_END         0x66        /* End of sound data */

/* Track indices matching SH-2 COMM6 protocol */
#define D32_VGM_TRACK_CATHEDRAL 0
#define D32_VGM_TRACK_TRISTRAM  1
#define D32_VGM_TRACK_CATACOMBS 2
#define D32_VGM_TRACK_CAVES     3
#define D32_VGM_TRACK_HELL      4
#define D32_VGM_TRACK_VICTORY   5
#define D32_VGM_TRACK_COUNT     6
#define D32_VGM_TRACK_MUTE      0xFF

#define D32_VGM_SAMPLES_PER_FRAME_60HZ 735

typedef struct {
    uint32_t magic;
    uint32_t eof_offset;
    uint32_t version;
    uint32_t sn76489_clock;
    uint32_t ym2413_clock;
    uint32_t gd3_offset;
    uint32_t total_samples;
    uint32_t loop_offset;
    uint32_t loop_samples;
    uint32_t rate;
    uint32_t ym2612_clock;
    uint32_t data_offset;
} D32VgmHeader;

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t data_offset;
    size_t loop_offset;
    size_t cur_pos;
    int32_t wait_samples;
    uint8_t playing;
    uint8_t loopable;
} D32VgmPlayer;

/* Callback signature for register writes: chip 0 = YM2612 Port 0, 1 = Port 1, 2 = PSG */
typedef void (*d32_vgm_write_cb)(void *user, uint8_t chip, uint8_t reg, uint8_t val);

/* Parse and validate a VGM header from raw file bytes. Returns 0 on success, < 0 on error. */
int d32_vgm_parse_header(const uint8_t *buf, size_t size, D32VgmHeader *out_hdr);

/* Initialize player state with a validated VGM stream. Returns 0 on success. */
int d32_vgm_player_init(D32VgmPlayer *player, const uint8_t *buf, size_t size);

/* Advance player by one 60Hz frame (735 samples). Returns number of commands executed. */
int d32_vgm_player_tick(D32VgmPlayer *player, d32_vgm_write_cb cb, void *user);

/* Stop and reset player */
void d32_vgm_player_stop(D32VgmPlayer *player);

#endif /* D32_VGM_H */
