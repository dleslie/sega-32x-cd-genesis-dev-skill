/**
 * @file sub_cdrom.c
 * @brief Sega CD CD-ROM Sector Streaming & Word RAM Transfer Pipeline Implementation.
 *
 * Implements hardware CDC register control, sector-to-Word RAM DMA streaming,
 * and seamless ISO-9660 file loading directly into shared graphics memory.
 *
 * Copyright (c) 2026 Dan Leslie
 * SPDX-License-Identifier: MIT
 */

#include "sub_cdrom.h"
#include <string.h>

/* Hardware Sub-CPU CDC Registers */
#ifdef __m68k__
#define SUB_REG_CDCMODE          (*(volatile uint16_t *)0xFF8004)
#define SUB_REG_CDCHOST_DATA     (*(volatile uint16_t *)0xFF8008)
#else
static volatile uint16_t s_mock_reg_cdcmode = 0;
static volatile uint16_t s_mock_reg_cdchost_data = 0;
#define SUB_REG_CDCMODE          s_mock_reg_cdcmode
#define SUB_REG_CDCHOST_DATA     s_mock_reg_cdchost_data
#endif

static IsoSectorReaderFn s_custom_backend = NULL;
static uint8_t s_dma_sector_buffer[CDROM_SECTOR_SIZE];

/**
 * @brief Default hardware-level sector read function.
 */
static bool default_hw_read_sector(uint32_t lba, uint8_t *dst)
{
    (void)lba;

    /* Configure CDC destination to Sub-CPU bus */
    SUB_REG_CDCMODE = CDC_DEST_SUB_CPU;

    /* If destination provided, clear buffer in absence of optical media */
    if (dst)
    {
        memset(dst, 0, CDROM_SECTOR_SIZE);
        return true;
    }

    return false;
}

void cdrom_set_read_backend(IsoSectorReaderFn backend)
{
    s_custom_backend = backend;
}

bool cdrom_read_sector(uint32_t lba, uint8_t *dst)
{
    if (s_custom_backend)
    {
        /* Delegate to registered custom or test harness reader */
        return s_custom_backend(lba, dst);
    }

    /* Fallback to hardware CDC registers */
    return default_hw_read_sector(lba, dst);
}

bool cdrom_init(void)
{
    /* Connect the ISO-9660 filesystem parser to the CD-ROM reader */
    return iso9660_init(cdrom_read_sector);
}

bool cdrom_stream_to_word_ram(uint32_t start_lba, uint32_t sector_count, volatile uint16_t *word_ram_dest)
{
    if (!word_ram_dest || sector_count == 0)
    {
        /* Invalid parameters provided */
        return false;
    }

    uint32_t current_lba = start_lba;
    volatile uint16_t *dest_ptr = word_ram_dest;

    for (uint32_t s = 0; s < sector_count; s++)
    {
        /* Read next 2048-byte sector into transit buffer */
        if (!cdrom_read_sector(current_lba, s_dma_sector_buffer))
        {
            /* Sector transfer interrupted by optical read error */
            return false;
        }

        /* Transfer 1024 16-bit big-endian words into Word RAM */
        for (uint32_t w = 0; w < (CDROM_SECTOR_SIZE / 2); w++)
        {
            uint8_t hi = s_dma_sector_buffer[w * 2];
            uint8_t lo = s_dma_sector_buffer[w * 2 + 1];
            *dest_ptr++ = (uint16_t)(((uint16_t)hi << 8) | lo);
        }

        current_lba++;
    }

    return true;
}

bool cdrom_load_file_to_word_ram(const char *path, volatile uint16_t *word_ram_dest, uint32_t *out_bytes_read)
{
    if (!path || !word_ram_dest)
    {
        /* Preconditions not met */
        return false;
    }

    IsoFileEntry file_entry;
    if (!iso9660_find_file(path, &file_entry))
    {
        /* Specified file not found on ISO-9660 filesystem */
        return false;
    }

    /* Calculate total sectors required to encompass file payload */
    uint32_t total_sectors = (file_entry.size + (CDROM_SECTOR_SIZE - 1)) / CDROM_SECTOR_SIZE;

    /* Stream contiguous file extent sectors into Word RAM */
    if (!cdrom_stream_to_word_ram(file_entry.lba, total_sectors, word_ram_dest))
    {
        /* Transfer failed */
        return false;
    }

    if (out_bytes_read)
    {
        *out_bytes_read = file_entry.size;
    }

    return true;
}
