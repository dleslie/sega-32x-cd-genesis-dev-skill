/**
 * @file everdrive.c
 * @brief Mega EverDrive PRO / X7 EDIO & USB Debug Implementation.
 *
 * SPDX-License-Identifier: MIT
 */

#include "everdrive.h"

bool evd_init(void)
{
    /* Unlock key sequence */
    EVD_REG_CFG = 0x1234;

    /* Check status register response */
    uint16_t status = EVD_REG_STATUS;
    if ((status & 0xFF00) == 0xFF00)
    {
        return false; /* Open bus or inactive */
    }

    return true;
}

void evd_put_char(char c)
{
    /* Wait until FIFO transmit buffer has space */
    while (!(EVD_REG_STATUS & EVD_STAT_FIFO_TXE))
    {
        __asm__ volatile ("nop");
    }
    EVD_REG_USB_DATA = (uint16_t)(uint8_t)c;
}

void evd_puts(const char *str)
{
    if (!str) return;
    while (*str)
    {
        evd_put_char(*str++);
    }
}

bool evd_read_sectors(uint32_t sector_lba, void *dst_buffer, uint16_t sector_count)
{
    if (!dst_buffer || sector_count == 0)
    {
        return false;
    }

    uint16_t *dst = (uint16_t *)dst_buffer;

    for (uint16_t s = 0; s < sector_count; s++)
    {
        /* Issue read command for LBA */
        uint32_t cur_lba = sector_lba + s;
        EVD_REG_CMD = 0x0010; /* CMD_SD_READ */
        EVD_REG_DATA = (uint16_t)(cur_lba >> 16);
        EVD_REG_DATA = (uint16_t)(cur_lba & 0xFFFF);

        /* Wait for SPI transfer ready */
        while (EVD_REG_STATUS & EVD_STAT_SPI_BUSY)
        {
            __asm__ volatile ("nop");
        }

        /* Read 256 words (512 bytes) from FIFO */
        for (int i = 0; i < 256; i++)
        {
            *dst++ = EVD_REG_DATA;
        }
    }

    return true;
}
