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

#define CMD_INIT             0x0001
#define CMD_RENDER_FRAME     0x0002
#define CMD_BRAM_SAVE        0x0004
#define CMD_BRAM_LOAD        0x0005
#define CMD_BRAM_FORMAT      0x0006

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

/* Transformation State */
#define SCALE_MIN_Q8         64     /* 0.25x magnification */
#define SCALE_MAX_Q8         512    /* 2.00x magnification */
#define SCALE_STEP_Q8        4      /* Step increment per input tick */

static uint32_t s_cd_frame_count = 0;
static uint8_t  s_rotation_angle = 0;
static uint16_t s_scale_q8 = 256;   /* 1.00x normal scale */
static int16_t  s_scale_velocity = 2;

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
 * @brief Updates affine transform angle and zoom based on joypad input or automatic cycling.
 * Avoids branch penalties where possible.
 */
static void update_transform_state(uint16_t pad)
{
    /* Manual rotation via D-Pad Left / Right */
    if (pad & PAD_LEFT)
    {
        /* Counter-clockwise rotation requested */
        s_rotation_angle = (uint8_t)(s_rotation_angle - 2);
    }
    else if (pad & PAD_RIGHT)
    {
        /* Clockwise rotation requested */
        s_rotation_angle = (uint8_t)(s_rotation_angle + 2);
    }
    else
    {
        /* Default continuous rotation when idle */
        s_rotation_angle = (uint8_t)(s_rotation_angle + 1);
    }

    /* Manual zoom via D-Pad Up / Down */
    if (pad & PAD_UP)
    {
        /* Zoom in towards target */
        s_scale_q8 = (s_scale_q8 < SCALE_MAX_Q8) ? (s_scale_q8 + SCALE_STEP_Q8) : SCALE_MAX_Q8;
    }
    else if (pad & PAD_DOWN)
    {
        /* Zoom out away from target */
        s_scale_q8 = (s_scale_q8 > SCALE_MIN_Q8) ? (s_scale_q8 - SCALE_STEP_Q8) : SCALE_MIN_Q8;
    }
    else
    {
        /* Automatic gentle breathing / pulsing zoom when no manual zoom buttons are held */
        int32_t next_scale = (int32_t)s_scale_q8 + s_scale_velocity;
        if (next_scale >= 384)
        {
            /* Reverse zoom direction when reaching magnification ceiling */
            s_scale_velocity = -2;
            next_scale = 384;
        }
        else if (next_scale <= 160)
        {
            /* Reverse zoom direction when reaching magnification floor */
            s_scale_velocity = 2;
            next_scale = 160;
        }
        s_scale_q8 = (uint16_t)next_scale;
    }
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

    /* Verify Backup RAM format integrity on startup */
    if (!sub_bram_is_formatted())
    {
        sub_bram_format();
    }

    /* Begin looping background audio playback on PCM Channel 0 */
    sub_pcm_play(0, 0x0000, 0x0000, PCM_PITCH_NORMAL, 0xC0, PCM_PAN_CENTER);

    /* Inform Genesis Main 68000 that Sub-CPU is initialized and ready */
    SUB_COM_STAT0 = STAT_READY;

    while (1)
    {
        uint16_t cmd = SUB_COM_CMD0;

        /* Check for render requests from the Genesis master coordinator */
        if (cmd == CMD_RENDER_FRAME)
        {
            uint16_t pad = SUB_COM_CMD2;

            /* 1. Update transformation angle and zoom parameters */
            update_transform_state(pad);

            /* 2. Modulate PCM pitch with zoom and stereo pan with rotation angle */
            uint16_t pcm_pitch = (uint16_t)(((uint32_t)s_scale_q8 * PCM_PITCH_NORMAL) >> 8);
            sub_pcm_set_pitch(0, pcm_pitch);

            int16_t cos_val = sub_cos(s_rotation_angle);
            if (cos_val > 2048) cos_val = 2048;
            else if (cos_val < -2048) cos_val = -2048;
            uint8_t right_vol = (uint8_t)(((int32_t)(cos_val + 2048) * 15 + 2048) >> 12);
            if (right_vol > 15) right_vol = 15;
            uint8_t left_vol = (uint8_t)(15 - right_vol);
            sub_pcm_set_pan(0, (uint8_t)((left_vol << 4) | (right_vol & 0x0F)));

            /* 3. Dispatch affine transformation pass to hardware ASIC */
            sub_asic_render_transform(s_rotation_angle, s_scale_q8);

            /* 4. Wait for ASIC raster engine to finish writing image into Word RAM */
            sub_asic_wait_idle();

            s_cd_frame_count++;

            /* 5. Telemetry dispatched to Genesis Main-CPU */
            SUB_COM_STAT1 = (uint16_t)(s_cd_frame_count & 0xFFFF);
            SUB_COM_STAT2 = (uint16_t)s_rotation_angle;
            SUB_COM_STAT3 = s_scale_q8;

            /* 6. Handshake completed buffer back to Genesis VDP via Word RAM bank swap */
            sub_swap_word_ram();
        }
        else if (cmd == CMD_BRAM_SAVE)
        {
            /* Persist game state snapshot into Backup RAM */
            SegaCD_SavePayload payload;
            payload.magic = 0x53434453;
            payload.angle = (uint16_t)s_rotation_angle;
            payload.scale = s_scale_q8;
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
                    s_rotation_angle = (uint8_t)payload.angle;
                    s_scale_q8 = payload.scale;
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
            s_rotation_angle = 0;
            s_scale_q8 = 256;
            SUB_COM_STAT1 = 0;
            SUB_COM_STAT2 = 0;
            SUB_COM_STAT3 = 256;
            sub_asic_init();
            sub_pcm_init();
            sub_pcm_stage_synth_tone();
            sub_pcm_play(0, 0x0000, 0x0000, PCM_PITCH_NORMAL, 0xC0, PCM_PAN_CENTER);
        }
    }
}
