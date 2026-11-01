/**
 * @file sub_main.c
 * @brief Sega CD Sub-CPU 68000 Co-Processor Main Routine.
 *
 * Coordinates:
 *   - Ricoh RF5C164 8-channel PCM sound chip initialization
 *   - ASIC affine coprocessor (hardware rotation, zoom, stamp rendering)
 *   - COMM register protocol handshake with Genesis Main 68000
 *   - Word RAM 1M Mode double-buffer rendering (ping-pong swapping)
 *   - Real-time transformation telemetry dispatched to Main-CPU
 *
 * Copyright (c) 2026 Dan Leslie
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdbool.h>
#include "sub_asic.h"
#include "sub_pcm.h"
#include "sub_bram.h"
#include "sub_iso9660.h"
#include "sub_cdda.h"
#include "sub_cdrom.h"

/* Sub-CPU Hardware Register Aperture (0xFF8000) */
#define SUB_REG_RESET        (*(volatile uint16_t *)0xFF8000)
#define SUB_REG_MEMMODE      (*(volatile uint16_t *)0xFF8002)
#define SUB_REG_CDCMODE      (*(volatile uint16_t *)0xFF8004)

/* Communication Registers (Sub-CPU View) */
#define SUB_COM_CMD0         (*(volatile uint16_t *)0xFF8010)
#define SUB_COM_CMD1         (*(volatile uint16_t *)0xFF8012)
#define SUB_COM_CMD2         (*(volatile uint16_t *)0xFF8014)
#define SUB_COM_CMD3         (*(volatile uint16_t *)0xFF8016)

#define SUB_COM_STAT0        (*(volatile uint16_t *)0xFF8020)
#define SUB_COM_STAT1        (*(volatile uint16_t *)0xFF8022)
#define SUB_COM_STAT2        (*(volatile uint16_t *)0xFF8024)
#define SUB_COM_STAT3        (*(volatile uint16_t *)0xFF8026)

/* Word RAM 1M Mode Aperture (Sub-CPU View: 128 KB Bank) */
#define SUB_WORD_RAM_1M      ((volatile uint16_t *)0x0C0000)

/* Protocol Constants */
#define STAT_READY           0x0001
#define STAT_BRAM_SAVED      0x0010
#define STAT_BRAM_LOADED     0x0020
#define STAT_BRAM_FORMATTED  0x0030
#define STAT_CDDA_ACTIVE     0x0040

#define CMD_INIT             0x0001
#define CMD_RENDER_FRAME     0x0002
#define CMD_BRAM_SAVE        0x0004
#define CMD_BRAM_LOAD        0x0005
#define CMD_BRAM_FORMAT      0x0006
#define CMD_CDDA_PLAY        0x0011
#define CMD_CDDA_STOP        0x0012
#define CMD_CDDA_PAUSE       0x0013
#define CMD_CDDA_RESUME      0x0014

/* BRAM Save State Payload Structure */
typedef struct {
    uint32_t magic;      /**< 0x53434453 ("SCDS") */
    uint16_t angle;      /**< Current rotation angle */
    uint16_t scale;      /**< Current zoom factor   */
    uint32_t score;      /**< Frame count / score   */
} SegaCD_SavePayload;

/* Joypad Input Bitmasks forwarded from Genesis Main-CPU */
#define PAD_UP               0x0001
#define PAD_DOWN             0x0002
#define PAD_LEFT             0x0004
#define PAD_RIGHT            0x0008
#define PAD_DIR_MASK         0x000F

/* Road Simulation State */
#define SPEED_MIN            32     /* Minimum cruising speed */
#define SPEED_MAX            192    /* Maximum hyper-turbo speed */
#define SPEED_DEFAULT        104    /* High-velocity default cruising speed */

static uint32_t s_cd_frame_count = 0;
static uint16_t s_road_z = 0;
static int16_t  s_steer_x = 0;
static int16_t  s_speed = SPEED_DEFAULT;
static int16_t  s_curve_amt = 0;
static uint8_t  s_bank_angle = 0;

/**
 * @brief Requests ownership swap of the current 128 KB Word RAM bank.
 * In 1M mode, toggling the RET bit signals the Genesis Main-CPU that this bank
 * is complete and ready for Genesis VDP DMA transfer.
 */
void sub_swap_word_ram(void)
{
    /* Invert RET bit (bit 0) to request ownership swap */
    uint16_t val = SUB_REG_MEMMODE;
    SUB_REG_MEMMODE = val ^ 0x0001;

    /* Spin until memory controller acknowledges the handover to the Genesis */
    while ((SUB_REG_MEMMODE & 0x0001) == (val & 0x0001))
    {
        __asm__ volatile ("nop");
    }
}

/**
 * @brief Updates banked road physics, curvature, camera steering, and speed.
 */
static void update_road_state(uint16_t pad)
{
    /* 1. Procedural Road S-Curves: modulate curve coefficient based on travel distance */
    int16_t wave = sub_sin((uint8_t)(s_cd_frame_count >> 1));
    s_curve_amt = (int16_t)((wave * 36) >> 11); /* -36..+36 deflection */

    /* 2. Steering & Lateral Displacement (Left / Right) */
    if (pad & PAD_LEFT)
    {
        /* Steer Left: displace camera right relative to road center */
        if (s_steer_x < 56)
        {
            s_steer_x += 2;
        }
        /* Tilt road into the left turn */
        int8_t signed_bank = (int8_t)s_bank_angle;
        if (signed_bank < 18)
        {
            s_bank_angle = (uint8_t)(signed_bank + 1);
        }
    }
    else if (pad & PAD_RIGHT)
    {
        /* Steer Right: displace camera left relative to road center */
        if (s_steer_x > -56)
        {
            s_steer_x -= 2;
        }
        /* Tilt road into the right turn */
        int8_t signed_bank = (int8_t)s_bank_angle;
        if (signed_bank > -18)
        {
            s_bank_angle = (uint8_t)(signed_bank - 1);
        }
    }
    else
    {
        /* Gently recenter lateral steering and bank automatically into S-curves */
        if (s_steer_x > 0)
        {
            s_steer_x--;
        }
        else if (s_steer_x < 0)
        {
            s_steer_x++;
        }

        int8_t target_bank = (int8_t)(s_curve_amt >> 2); /* -9..+9 automatic curve banking */
        int8_t signed_bank = (int8_t)s_bank_angle;
        if (signed_bank < target_bank)
        {
            s_bank_angle = (uint8_t)(signed_bank + 1);
        }
        else if (signed_bank > target_bank)
        {
            s_bank_angle = (uint8_t)(signed_bank - 1);
        }
    }

    /* 3. Speed & Acceleration (Up / Down) */
    if (pad & PAD_UP)
    {
        if (s_speed < SPEED_MAX)
        {
            s_speed += 2;
        }
    }
    else if (pad & PAD_DOWN)
    {
        if (s_speed > SPEED_MIN)
        {
            s_speed -= 2;
        }
    }
    else
    {
        /* Return towards cruising speed */
        if (s_speed < SPEED_DEFAULT)
        {
            s_speed++;
        }
        else if (s_speed > SPEED_DEFAULT)
        {
            s_speed--;
        }
    }

    /* 4. Advance forward road coordinate at high velocity so road stripes and skyscrapers rush toward camera */
    s_road_z = (s_road_z + (s_speed >> 2)) & 0xFF;
}

/**
 * @brief Sega CD Sub-CPU 68000 main loop.
 * Executes continuously in 512 KB PRG-RAM at 12.5 MHz.
 */
void sub_main(void)
{
    sub_pcm_init();
    sub_pcm_stage_synth_tone();
    sub_asic_init();
    cdda_init();
    cdrom_init();

    /* Verify Backup RAM format integrity on startup */
    if (!sub_bram_is_formatted())
    {
        sub_bram_format();
    }

    /* Begin looping background audio playback on PCM Channel 0 */
    sub_pcm_play(0, 0x0000, 0x0000, PCM_PITCH_NORMAL, 0xC0, PCM_PAN_CENTER);

    /* Render initial road frame into Word RAM before signaling ready */
    sub_asic_render_banked_road(s_road_z, s_steer_x, s_curve_amt, s_bank_angle);

    /* Inform Genesis Main 68000 that Sub-CPU is initialized and ready */
    SUB_COM_STAT0 = STAT_READY;

    while (1)
    {
        uint16_t cmd = SUB_COM_CMD0;

        /* Check for render requests from the Genesis master coordinator */
        if (cmd == CMD_RENDER_FRAME)
        {
            uint16_t pad = SUB_COM_CMD2;

            /* 1. Update banked road physics, curvature, camera steering, and speed */
            update_road_state(pad);

            /* 2. Modulate PCM audio pitch with driving speed (2048 = 1.0x normal pitch) */
            uint16_t pcm_pitch = (uint16_t)(1024 + ((uint32_t)s_speed * 16));
            sub_pcm_set_pitch(0, pcm_pitch);

            /* Stereo panning follows camera steering */
            int32_t pan_bias = (int32_t)s_steer_x; /* -64..+64 */
            uint8_t left_vol  = (uint8_t)((64 - pan_bias) * 15 / 128);
            uint8_t right_vol = (uint8_t)((64 + pan_bias) * 15 / 128);
            if (left_vol > 15) left_vol = 15;
            if (right_vol > 15) right_vol = 15;
            sub_pcm_set_pan(0, (uint8_t)((left_vol << 4) | (right_vol & 0x0F)));

            /* 3. Dispatch pseudo-3D curved/banked road rasterization to hardware ASIC */
            sub_asic_render_banked_road(s_road_z, s_steer_x, s_curve_amt, s_bank_angle);

            /* 4. Wait for ASIC raster engine to finish writing image into Word RAM */
            sub_asic_wait_idle();

            s_cd_frame_count++;

            /* 5. Telemetry dispatched to Genesis Main-CPU */
            SUB_COM_STAT1 = (uint16_t)(s_cd_frame_count & 0xFFFF);
            SUB_COM_STAT2 = (uint16_t)(((uint16_t)(uint8_t)s_curve_amt << 8) | ((uint8_t)s_speed));
            SUB_COM_STAT3 = (uint16_t)(((uint16_t)(uint8_t)s_bank_angle << 8) | ((uint8_t)s_steer_x));

            /* 6. Update CD-DA streaming head position */
            cdda_update(1);

            /* 7. Handshake completed buffer back to Genesis VDP via Word RAM bank swap */
            sub_swap_word_ram();
        }
        else if (cmd == CMD_CDDA_PLAY)
        {
            /* Play specified CD-DA audio track */
            uint8_t track = (uint8_t)SUB_COM_CMD1;
            cdda_play_track(track);
            SUB_COM_STAT0 = STAT_CDDA_ACTIVE;
        }
        else if (cmd == CMD_CDDA_STOP)
        {
            /* Halt CD-DA playback */
            cdda_stop();
            SUB_COM_STAT0 = STAT_READY;
        }
        else if (cmd == CMD_CDDA_PAUSE)
        {
            /* Pause CD-DA streaming */
            cdda_pause();
            SUB_COM_STAT0 = STAT_READY;
        }
        else if (cmd == CMD_CDDA_RESUME)
        {
            /* Resume CD-DA streaming */
            cdda_resume();
            SUB_COM_STAT0 = STAT_CDDA_ACTIVE;
        }
        else if (cmd == CMD_BRAM_SAVE)
        {
            /* Persist game state snapshot into Backup RAM */
            SegaCD_SavePayload payload;
            payload.magic = 0x53434453;
            payload.angle = (uint16_t)s_bank_angle;
            payload.scale = (uint16_t)s_speed;
            payload.score = s_cd_frame_count;

            sub_bram_save_file("DEMO_SAVE", "SLOT 1", &payload, sizeof(payload));
            SUB_COM_STAT0 = STAT_BRAM_SAVED;
        }
        else if (cmd == CMD_BRAM_LOAD)
        {
            /* Restore game state snapshot from Backup RAM */
            SegaCD_SavePayload payload;
            uint16_t loaded_size = 0;

            if (sub_bram_load_file("DEMO_SAVE", &payload, sizeof(payload), &loaded_size))
            {
                if (payload.magic == 0x53434453)
                {
                    s_bank_angle = (uint8_t)payload.angle;
                    s_speed = (int16_t)payload.scale;
                    SUB_COM_STAT0 = STAT_BRAM_LOADED;
                }
            }
        }
        else if (cmd == CMD_BRAM_FORMAT)
        {
            /* Re-initialize Backup RAM structure */
            sub_bram_format();
            SUB_COM_STAT0 = STAT_BRAM_FORMATTED;
        }
        else if (cmd == CMD_INIT)
        {
            /* Acknowledge re-initialization command */
            SUB_COM_STAT0 = STAT_READY;
            s_cd_frame_count = 0;
            s_road_z = 0;
            s_steer_x = 0;
            s_speed = SPEED_DEFAULT;
            s_curve_amt = 0;
            s_bank_angle = 0;
            SUB_COM_STAT1 = 0;
            SUB_COM_STAT2 = (uint16_t)SPEED_DEFAULT;
            SUB_COM_STAT3 = 0;
            sub_asic_init();
            sub_pcm_init();
            sub_pcm_stage_synth_tone();
            sub_pcm_play(0, 0x0000, 0x0000, PCM_PITCH_NORMAL, 0xC0, PCM_PAN_CENTER);
        }
    }
}
