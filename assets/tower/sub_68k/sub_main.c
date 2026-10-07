/**
 * @file sub_main.c
 * @brief Sega CD Sub-CPU 68000 Main Execution Loop (Tower of Power Edition).
 *
 * Coordinates CD-ROM streaming, Word RAM 1M mode graphics buffering,
 * ASIC affine transformations, and Ricoh RF5C164 PCM audio playback.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdbool.h>

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

/* Word RAM Base Pointer (1M Mode Sub-CPU Window) */
#define SUB_WORD_RAM_1M      ((volatile uint16_t *)0x0C0000)

/* Ricoh RF5C164 PCM Sound Chip Registers */
#define SUB_PCM_CTRL         (*(volatile uint8_t *)0xFF0001)
#define SUB_PCM_CHAN_ENABLE  (*(volatile uint8_t *)0xFF0003)

static uint32_t s_cd_frame_count = 0;

void sub_init_pcm(void)
{
    /* Enable Ricoh PCM sound chip */
    SUB_PCM_CTRL = 0x80; /* Start sound chip operation */
}

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

void sub_main(void)
{
    sub_init_pcm();

    /* Acknowledge Genesis Main-CPU initialization */
    SUB_COM_STAT0 = 0x0001; /* STAT_READY */

    while (1)
    {
        /* Check for command from Genesis Main-CPU */
        uint16_t cmd = SUB_COM_CMD0;
        uint16_t arg = SUB_COM_CMD1;

        if (cmd == 0x0002) /* CMD_RENDER_FRAME */
        {
            /* Write stamped background data into Word RAM buffer */
            volatile uint16_t *wram = SUB_WORD_RAM_1M;
            uint16_t pattern = (uint16_t)(s_cd_frame_count & 0xFFFF);

            /* Fill initial tiles for Genesis VDP DMA transfer */
            for (int i = 0; i < 256; i++)
            {
                wram[i] = pattern + i;
            }

            s_cd_frame_count++;
            SUB_COM_STAT1 = (uint16_t)(s_cd_frame_count & 0xFFFF);

            /* Handshake bank swap back to Genesis */
            sub_swap_word_ram();
        }

        SUB_COM_STAT3 = (uint16_t)(s_cd_frame_count & 0xFFFF);
    }
}
