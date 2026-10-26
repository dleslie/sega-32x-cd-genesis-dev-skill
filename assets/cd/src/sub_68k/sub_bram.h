/**
 * @file sub_bram.h
 * @brief Sega CD Sub-CPU Backup RAM (BRAM) File System Driver.
 *
 * Provides block-level allocation, filesystem formatting, directory scanning,
 * and save file serialization for the 16 KB / 8 KB battery-backed SRAM at 0xFE0000.
 *
 * Copyright (c) 2026 Dan Leslie
 * SPDX-License-Identifier: MIT
 */

#ifndef SUB_BRAM_H
#define SUB_BRAM_H

#include <stdint.h>
#include <stdbool.h>

/* --- Hardware Memory Aperture (Sub-CPU Bus: Odd Bytes on 0xFE0000) --- */
#define BRAM_BASE_ADDR           0xFE0001
#define BRAM_SIZE_BYTES          8192
#define BRAM_BLOCK_SIZE          64
#define BRAM_TOTAL_BLOCKS        (BRAM_SIZE_BYTES / BRAM_BLOCK_SIZE) /* 128 blocks */

/* Block Indices */
#define BRAM_BLOCK_HEADER        0
#define BRAM_BLOCK_SEPARATOR     1
#define BRAM_BLOCK_DATA_START    2

/* File Header Flags */
#define BRAM_FLAG_FILE_START     0x80000000UL
#define BRAM_FLAG_FREE           0x00000000UL

/* Field Lengths */
#define BRAM_NAME_MAX_LEN        11
#define BRAM_DESC_MAX_LEN        10
#define BRAM_INLINE_PAYLOAD_MAX  (BRAM_BLOCK_SIZE - 34) /* 30 bytes inline */

/* Language Identifiers */
#define BRAM_LANG_JAPANESE       0x00
#define BRAM_LANG_ENGLISH        0x01
#define BRAM_LANG_FRENCH         0x02
#define BRAM_LANG_GERMAN         0x03
#define BRAM_LANG_SPANISH        0x04
#define BRAM_LANG_ITALIAN        0x05

/**
 * @brief Sega CD Standard Game Save Header Structure (34 bytes inside a 64-byte block).
 */
typedef struct {
    uint32_t flag;                      /**< 0x80000000 = Start of valid save file */
    char     name[BRAM_NAME_MAX_LEN];   /**< 11-byte ASCII filename identifier      */
    uint8_t  language;                  /**< Language code (e.g. 0x01 = English)   */
    char     desc[BRAM_DESC_MAX_LEN];   /**< 10-byte ASCII human-readable label     */
    uint32_t timestamp;                 /**< Timestamp or save sequence counter     */
    uint32_t size;                      /**< Payload size in bytes                  */
} __attribute__((packed)) SegaCD_BRAM_FileHeader;

/**
 * @brief Checks if Backup RAM contains the valid 64-byte Sega CD format signature.
 *
 * @return True if formatted, false otherwise.
 */
bool sub_bram_is_formatted(void);

/**
 * @brief Formats Backup RAM with the standard Sega CD format signature and zeros free blocks.
 */
void sub_bram_format(void);

/**
 * @brief Searches Backup RAM for a save file matching the given filename.
 *
 * @param name 11-byte maximum null-terminated ASCII string.
 * @return Starting block index (2..127) if found, or -1 if not found.
 */
int16_t sub_bram_find_file(const char *name);

/**
 * @brief Saves a record into Backup RAM, creating or updating an existing entry.
 *
 * @param name 11-byte maximum null-terminated ASCII string.
 * @param desc 10-byte maximum null-terminated description string.
 * @param data Pointer to payload data buffer.
 * @param size Size of payload in bytes (up to 30 bytes for single block).
 * @return True on successful write, false if out of space or parameters invalid.
 */
bool sub_bram_save_file(const char *name, const char *desc, const void *data, uint16_t size);

/**
 * @brief Loads a save record payload from Backup RAM into a destination buffer.
 *
 * @param name Filename to locate.
 * @param out_buf Destination buffer.
 * @param max_size Capacity of destination buffer.
 * @param out_size Pointer to receive the actual payload byte count (may be NULL).
 * @return True if located and loaded successfully, false otherwise.
 */
bool sub_bram_load_file(const char *name, void *out_buf, uint16_t max_size, uint16_t *out_size);

/**
 * @brief Deletes an existing save file from Backup RAM by clearing its header flag.
 *
 * @param name Filename to delete.
 * @return True if file existed and was deleted, false if file not found.
 */
bool sub_bram_delete_file(const char *name);

/**
 * @brief Counts the total number of free/available 64-byte blocks in Backup RAM.
 *
 * @return Number of free blocks (0..126).
 */
uint16_t sub_bram_get_free_blocks(void);

#endif /* SUB_BRAM_H */
