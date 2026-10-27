/**
 * @file sub_main.c
 * @brief Sega CD Sub-CPU 68000 Main Execution Loop (Tower of Power Edition).
 *
 * Coordinates:
 *   - Ricoh RF5C164 8-channel PCM sound chip initialization and real-time playback
 *   - Word RAM 1M mode double-buffer graphics rendering and bank swapping
 *   - COMM register protocol handshake with Genesis Main 68000
 *
 * Copyright (c) 2026 Dan Leslie
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdbool.h>
#include "sub_pcm.h"

/* Sub-CPU Hardware Registers (0xFF8000 aperture) */
#define SUB_REG_RESET        (*(volatile uint16_t *)0xFF8000)
#define SUB_REG_MEMMODE      (*(volatile uint16_t *)0xFF8002)
#define SUB_REG_CDCMODE      (*(volatile uint16_t *)0xFF8004)

#define SUB_COM_CMD0         (*(volatile uint16_t *)0xFF8010)
#define SUB_COM_CMD1         (*(volatile uint16_t *)0xFF8012)
#define SUB_COM_CMD2         (*(volatile uint16_t *)0xFF8014)
#define SUB_COM_CMD3         (*(volatile uint16_t *)0xFF8016)

#define SUB_COM_STAT0        (*(volatile uint16_t *)0xFF8020)
#define SUB_COM_STAT1        (*(volatile uint16_t *)0xFF8022)
#define SUB_COM_STAT2        (*(volatile uint16_t *)0xFF8024)
#define SUB_COM_STAT3        (*(volatile uint16_t *)0xFF8026)

/* Word RAM Base Pointer (1M Mode Sub-CPU Window: 128 KB Bank) */
#define SUB_WORD_RAM_1M      ((volatile uint16_t *)0x0C0000)

static uint32_t s_cd_frame_count = 0;

/**
 * @brief Requests ownership swap of the current 128 KB Word RAM bank.
 * Toggles the RET bit (bit 0) and waits for memory controller handover acknowledgment.
 */
void sub_swap_word_ram(void)
{
    /* Toggle RET bit on Sub-CPU side */
    uint16_t val = SUB_REG_MEMMODE;
    SUB_REG_MEMMODE = val ^ 0x0001;

    /* Wait for RET bit acknowledge */
    while ((SUB_REG_MEMMODE & 0x0001) == (val & 0x0001))
    {
        __asm__ volatile ("nop");
    }
}

/**
 * @brief Sega CD Sub-CPU 68000 main entry point for the Tower of Power stack.
 * Executes in 512 KB PRG-RAM at 12.5 MHz.
 */
void sub_main(void)
{
    /* 1. Initialize Ricoh RF5C164 sound processor and stage synth wave */
    sub_pcm_init();
    sub_pcm_stage_synth_tone();

    /* 2. Begin continuous looping audio playback on PCM Channel 0 */
    sub_pcm_play(0, 0x0000, 0x0000, PCM_PITCH_NORMAL, 0xD0, PCM_PAN_CENTER);

    /* 3. Acknowledge Genesis Main-CPU initialization */
    SUB_COM_STAT0 = 0x0001; /* STAT_READY */

    while (1)
    {
        /* Check for command from Genesis Main-CPU */
        uint16_t cmd = SUB_COM_CMD0;
        uint16_t pad = SUB_COM_CMD2;

        if (cmd == 0x0002) /* CMD_RENDER_FRAME */
        {
            /* 1. Modulate PCM pitch with frame counter and pad input */
            uint16_t pitch_offset = (uint16_t)((s_cd_frame_count & 0x7F) << 4);
            uint16_t pcm_pitch = PCM_PITCH_NORMAL + pitch_offset;
            if (pad & 0x0001) /* Up button held -> higher octave */
            {
                pcm_pitch += 0x0400;
            }
            else if (pad & 0x0002) /* Down button held -> lower octave */
            {
                pcm_pitch = (pcm_pitch > 0x0400) ? (pcm_pitch - 0x0400) : 0x0200;
            }
            sub_pcm_set_pitch(0, pcm_pitch);

            /* 2. Modulate stereo panning dynamically */
            uint8_t pan_step = (uint8_t)((s_cd_frame_count >> 1) & 0x0F);
            uint8_t pan_byte = (uint8_t)(((15 - pan_step) << 4) | (pan_step & 0x0F));
            sub_pcm_set_pan(0, pan_byte);

            /* 3. Write stamped background pattern into Word RAM buffer */
            volatile uint16_t *wram = SUB_WORD_RAM_1M;
            uint16_t pattern = (uint16_t)(s_cd_frame_count & 0xFFFF);

            /* Fill initial tiles for Genesis VDP DMA transfer */
            for (int i = 0; i < 256; i++)
            {
                wram[i] = pattern + i;
            }

            s_cd_frame_count++;
            SUB_COM_STAT1 = (uint16_t)(s_cd_frame_count & 0xFFFF);
            SUB_COM_STAT2 = pcm_pitch;

            /* 4. Handshake bank swap back to Genesis Main-CPU */
            sub_swap_word_ram();
        }

        SUB_COM_STAT3 = (uint16_t)(s_cd_frame_count & 0xFFFF);
    }
}
