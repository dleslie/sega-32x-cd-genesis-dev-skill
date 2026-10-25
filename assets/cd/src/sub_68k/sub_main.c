/**
 * @file sub_main.c
 * @brief Sega CD Sub-CPU 68000 Co-Processor Main Routine.
 *
 * Coordinates:
 *   - Ricoh RF5C164 8-channel PCM sound chip initialization
 *   - COMM register protocol handshake with Genesis Main 68000
 *   - Word RAM 1M Mode double-buffer rendering (ping-pong swapping)
 *   - Frame counter telemetry dispatched back to Main-CPU
 *
 * Copyright (c) 2026 Dan Leslie
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdbool.h>

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

/* Ricoh RF5C164 PCM Registers (Sub-CPU View) */
#define SUB_PCM_CTRL         (*(volatile uint8_t *)0xFF0001)
#define SUB_PCM_CHAN_ENABLE  (*(volatile uint8_t *)0xFF0003)

/* Protocol Constants */
#define STAT_READY           0x0001
#define CMD_INIT             0x0001
#define CMD_RENDER_FRAME     0x0002

static uint32_t s_cd_frame_count = 0;

/**
 * @brief Initializes the Ricoh RF5C164 8-channel PCM audio processor.
 * Starts sound chip clocking and silences channel outputs until samples are staged.
 */
void sub_init_pcm(void)
{
    /* Enable sound chip operation (bit 7 = 1) */
    SUB_PCM_CTRL = 0x80;
    /* Silence all 8 PCM channels initially */
    SUB_PCM_CHAN_ENABLE = 0xFF;
}

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
 * @brief Renders visual test patterns into the current Word RAM bank.
 * Produces structured 16-bit patterns that the Genesis Main-CPU can upload to VRAM.
 */
void sub_render_pattern(uint16_t frame_index)
{
    volatile uint16_t *wram = SUB_WORD_RAM_1M;

    /* Fill first 1024 words with an animated diagonal gradient and checksum */
    for (int i = 0; i < 1024; i++)
    {
        /* Blend frame counter with tile index to create an animated pattern */
        wram[i] = (uint16_t)((frame_index << 4) ^ (i & 0x0FFF));
    }
}

/**
 * @brief Sega CD Sub-CPU 68000 main loop.
 * Executes continuously in 512 KB PRG-RAM at 12.5 MHz.
 */
void sub_main(void)
{
    sub_init_pcm();

    /* Inform Genesis Main 68000 that Sub-CPU is initialized and ready */
    SUB_COM_STAT0 = STAT_READY;

    while (1)
    {
        uint16_t cmd = SUB_COM_CMD0;

        /* Check for render requests from the Genesis master coordinator */
        if (cmd == CMD_RENDER_FRAME)
        {
            /* Sub-CPU draws next frame buffer into the current 128 KB Word RAM bank */
            sub_render_pattern((uint16_t)(s_cd_frame_count & 0xFFFF));

            s_cd_frame_count++;
            SUB_COM_STAT1 = (uint16_t)(s_cd_frame_count & 0xFFFF);

            /* Handshake completed buffer back to Genesis VDP */
            sub_swap_word_ram();
        }
        else if (cmd == CMD_INIT)
        {
            /* Acknowledge re-initialization command */
            SUB_COM_STAT0 = STAT_READY;
            s_cd_frame_count = 0;
            SUB_COM_STAT1 = 0;
        }

        /* Telemetry heartbeat register */
        SUB_COM_STAT3 = (uint16_t)(s_cd_frame_count & 0xFFFF);
    }
}
