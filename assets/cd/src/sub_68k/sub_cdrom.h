/**
 * @file sub_cdrom.h
 * @brief Sega CD CD-ROM Sector Streaming & Word RAM Transfer Pipeline.
 *
 * Coordinates LC89515 CD-ROM Decoder (CDC) sector reading, ISO-9660 filesystem
 * integration, and direct burst DMA/streaming of textures and data into Word RAM.
 *
 * Copyright (c) 2026 Dan Leslie
 * SPDX-License-Identifier: MIT
 */

#ifndef SUB_CDROM_H
#define SUB_CDROM_H

#include <stdint.h>
#include <stdbool.h>
#include "sub_iso9660.h"

#define CDROM_SECTOR_SIZE        2048

/* CDC Mode Register (0xFF8004) Destination Bits */
#define CDC_DEST_MAIN_CPU        0x0000
#define CDC_DEST_SUB_CPU         0x0100
#define CDC_DEST_WORD_RAM        0x0200
#define CDC_DEST_DMA             0x0300

/**
 * @brief Initializes the CD-ROM sector reader and connects the ISO-9660 parser.
 *
 * @return true if hardware CDC and ISO-9660 filesystem were initialized successfully.
 */
bool cdrom_init(void);

/**
 * @brief Reads a single 2048-byte Mode 1 data sector from disc.
 *
 * @param lba Sector Logical Block Address.
 * @param dst 2048-byte buffer in memory.
 * @return true if sector was retrieved without error, false otherwise.
 */
bool cdrom_read_sector(uint32_t lba, uint8_t *dst);

/**
 * @brief Streams multiple contiguous sectors directly into a designated Word RAM buffer.
 *
 * @param start_lba Starting sector LBA.
 * @param sector_count Total sectors to stream.
 * @param word_ram_dest 16-bit word-aligned pointer in Word RAM (0x0C0000 or 0x080000).
 * @return true if all sectors were successfully transferred into Word RAM.
 */
bool cdrom_stream_to_word_ram(uint32_t start_lba, uint32_t sector_count, volatile uint16_t *word_ram_dest);

/**
 * @brief Locates a file via ISO-9660 and streams its contents directly into Word RAM.
 *
 * @param path File path (e.g. "/TEXTURES.BIN").
 * @param word_ram_dest 16-bit word-aligned destination in Word RAM.
 * @param out_bytes_read Receives actual count of bytes streamed into Word RAM.
 * @return true if file was found and completely transferred, false otherwise.
 */
bool cdrom_load_file_to_word_ram(const char *path, volatile uint16_t *word_ram_dest, uint32_t *out_bytes_read);

/**
 * @brief Allows registration of a custom low-level sector read backend (useful for tests or BIOS).
 *
 * @param backend Function pointer for reading 2048-byte sectors.
 */
void cdrom_set_read_backend(IsoSectorReaderFn backend);

#endif /* SUB_CDROM_H */
