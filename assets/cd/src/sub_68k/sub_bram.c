/**
 * @file sub_bram.c
 * @brief Sega CD Sub-CPU Backup RAM (BRAM) File System Implementation.
 *
 * Implements low-level odd-byte bus memory access, filesystem validation,
 * formatting, directory block lookup, file saving, loading, and deletion.
 *
 * Copyright (c) 2026 Dan Leslie
 * SPDX-License-Identifier: MIT
 */

#include "sub_bram.h"

/* Standard Sega CD Backup RAM Formatting Signature (16 bytes repeated 4 times = 64 bytes) */
static const char s_bram_format_signature[16] = "BackUpRam Format";

/* Internal save sequence counter */
static uint32_t s_bram_save_counter = 1;

/**
 * @brief Reads a single raw data byte from the odd-byte aligned Backup RAM aperture.
 */
static inline uint8_t bram_read_byte(uint16_t offset)
{
    volatile uint8_t *bram = (volatile uint8_t *)BRAM_BASE_ADDR;
    return bram[(uint32_t)offset * 2];
}

/**
 * @brief Writes a single raw data byte to the odd-byte aligned Backup RAM aperture.
 */
static inline void bram_write_byte(uint16_t offset, uint8_t val)
{
    volatile uint8_t *bram = (volatile uint8_t *)BRAM_BASE_ADDR;
    bram[(uint32_t)offset * 2] = val;
}

/**
 * @brief Reads a big-endian 16-bit word from Backup RAM.
 */
static inline uint16_t bram_read_be16(uint16_t offset)
{
    uint8_t hi = bram_read_byte(offset);
    uint8_t lo = bram_read_byte((uint16_t)(offset + 1));
    return (uint16_t)(((uint16_t)hi << 8) | lo);
}

/**
 * @brief Writes a big-endian 16-bit word to Backup RAM.
 */
static inline void bram_write_be16(uint16_t offset, uint16_t val)
{
    bram_write_byte(offset, (uint8_t)(val >> 8));
    bram_write_byte((uint16_t)(offset + 1), (uint8_t)(val & 0xFF));
}

/**
 * @brief Reads a big-endian 32-bit dword from Backup RAM.
 */
static inline uint32_t bram_read_be32(uint16_t offset)
{
    uint32_t hi = bram_read_be16(offset);
    uint32_t lo = bram_read_be16((uint16_t)(offset + 2));
    return (hi << 16) | lo;
}

/**
 * @brief Writes a big-endian 32-bit dword to Backup RAM.
 */
static inline void bram_write_be32(uint16_t offset, uint32_t val)
{
    bram_write_be16(offset, (uint16_t)(val >> 16));
    bram_write_be16((uint16_t)(offset + 2), (uint16_t)(val & 0xFFFF));
}

/**
 * @brief Checks if Backup RAM contains the valid 64-byte Sega CD format signature.
 */
bool sub_bram_is_formatted(void)
{
    /* Verify Block #0 contains 4 copies of 'BackUpRam Format' */
    for (uint16_t i = 0; i < BRAM_BLOCK_SIZE; i++)
    {
        uint8_t expected = (uint8_t)s_bram_format_signature[i & 0x0F];
        if (bram_read_byte(i) != expected)
        {
            /* Signature mismatch; memory requires formatting */
            return false;
        }
    }
    return true;
}

/**
 * @brief Formats Backup RAM with the standard Sega CD format signature and zeros free blocks.
 */
void sub_bram_format(void)
{
    /* 1. Write format signature to Block #0 (64 bytes) */
    for (uint16_t i = 0; i < BRAM_BLOCK_SIZE; i++)
    {
        bram_write_byte(i, (uint8_t)s_bram_format_signature[i & 0x0F]);
    }

    /* 2. Clear Block #1 separator / delimiter block (64 bytes of 0x00) */
    for (uint16_t i = 0; i < BRAM_BLOCK_SIZE; i++)
    {
        bram_write_byte((uint16_t)(BRAM_BLOCK_SIZE + i), 0x00);
    }

    /* 3. Initialize all data blocks (Blocks 2..127) as free */
    for (uint16_t b = BRAM_BLOCK_DATA_START; b < BRAM_TOTAL_BLOCKS; b++)
    {
        uint16_t block_offset = b * BRAM_BLOCK_SIZE;
        bram_write_be32(block_offset, BRAM_FLAG_FREE);
    }
}

/**
 * @brief Searches Backup RAM for a save file matching the given filename.
 */
int16_t sub_bram_find_file(const char *name)
{
    if (!name)
    {
        return -1;
    }

    /* Scan all data blocks from Block 2 to Block 127 */
    for (uint16_t b = BRAM_BLOCK_DATA_START; b < BRAM_TOTAL_BLOCKS; b++)
    {
        uint16_t block_offset = b * BRAM_BLOCK_SIZE;
        uint32_t flag = bram_read_be32(block_offset);

        /* Check if block is the start of a valid game save file */
        if (flag == BRAM_FLAG_FILE_START)
        {
            bool match = true;
            for (uint16_t i = 0; i < BRAM_NAME_MAX_LEN; i++)
            {
                char expected = name[i];
                char actual = (char)bram_read_byte((uint16_t)(block_offset + 4 + i));

                if (expected == '\0')
                {
                    /* End of search string; check if stored string is also padded */
                    if (actual != '\0' && actual != ' ')
                    {
                        match = false;
                    }
                    break;
                }

                if (actual != expected)
                {
                    match = false;
                    break;
                }
            }

            if (match)
            {
                return (int16_t)b;
            }
        }
    }

    return -1;
}

/**
 * @brief Saves a record into Backup RAM, creating or updating an existing entry.
 */
bool sub_bram_save_file(const char *name, const char *desc, const void *data, uint16_t size)
{
    if (!name || !data || size == 0)
    {
        return false;
    }

    /* Auto-format Backup RAM if not yet initialized */
    if (!sub_bram_is_formatted())
    {
        sub_bram_format();
    }

    /* 1. Locate existing file or find first free block */
    int16_t target_block = sub_bram_find_file(name);

    if (target_block < 0)
    {
        /* Find first free block */
        for (uint16_t b = BRAM_BLOCK_DATA_START; b < BRAM_TOTAL_BLOCKS; b++)
        {
            uint16_t offset = b * BRAM_BLOCK_SIZE;
            if (bram_read_be32(offset) == BRAM_FLAG_FREE)
            {
                target_block = (int16_t)b;
                break;
            }
        }
    }

    if (target_block < 0)
    {
        /* Out of free blocks in Backup RAM */
        return false;
    }

    uint16_t block_offset = (uint16_t)target_block * BRAM_BLOCK_SIZE;

    /* 2. Write File Header Flag */
    bram_write_be32(block_offset, BRAM_FLAG_FILE_START);

    /* 3. Write 11-byte ASCII Filename */
    for (uint16_t i = 0; i < BRAM_NAME_MAX_LEN; i++)
    {
        char ch = (name[i] != '\0') ? name[i] : ' ';
        bram_write_byte((uint16_t)(block_offset + 4 + i), (uint8_t)ch);
        if (name[i] == '\0')
        {
            /* Fill remainder with padding */
            for (uint16_t j = i + 1; j < BRAM_NAME_MAX_LEN; j++)
            {
                bram_write_byte((uint16_t)(block_offset + 4 + j), (uint8_t)' ');
            }
            break;
        }
    }

    /* 4. Write Language Code (English) */
    bram_write_byte((uint16_t)(block_offset + 15), BRAM_LANG_ENGLISH);

    /* 5. Write 10-byte Description Label */
    const char *desc_ptr = desc ? desc : "SAVE DATA ";
    for (uint16_t i = 0; i < BRAM_DESC_MAX_LEN; i++)
    {
        char ch = (desc_ptr[i] != '\0') ? desc_ptr[i] : ' ';
        bram_write_byte((uint16_t)(block_offset + 16 + i), (uint8_t)ch);
        if (desc_ptr[i] == '\0')
        {
            for (uint16_t j = i + 1; j < BRAM_DESC_MAX_LEN; j++)
            {
                bram_write_byte((uint16_t)(block_offset + 16 + j), (uint8_t)' ');
            }
            break;
        }
    }

    /* 6. Write Timestamp / Sequence Counter and Payload Length */
    bram_write_be32((uint16_t)(block_offset + 26), s_bram_save_counter++);
    bram_write_be32((uint16_t)(block_offset + 30), (uint32_t)size);

    /* 7. Write Inline Payload Data (up to 30 bytes) */
    const uint8_t *src_bytes = (const uint8_t *)data;
    uint16_t write_count = (size <= BRAM_INLINE_PAYLOAD_MAX) ? size : BRAM_INLINE_PAYLOAD_MAX;

    for (uint16_t i = 0; i < write_count; i++)
    {
        bram_write_byte((uint16_t)(block_offset + 34 + i), src_bytes[i]);
    }

    return true;
}

/**
 * @brief Loads a save record payload from Backup RAM into a destination buffer.
 */
bool sub_bram_load_file(const char *name, void *out_buf, uint16_t max_size, uint16_t *out_size)
{
    if (!name || !out_buf || max_size == 0)
    {
        return false;
    }

    int16_t target_block = sub_bram_find_file(name);
    if (target_block < 0)
    {
        return false;
    }

    uint16_t block_offset = (uint16_t)target_block * BRAM_BLOCK_SIZE;
    uint32_t file_size = bram_read_be32((uint16_t)(block_offset + 30));

    if (out_size)
    {
        *out_size = (uint16_t)file_size;
    }

    uint16_t read_count = (file_size <= max_size) ? (uint16_t)file_size : max_size;
    if (read_count > BRAM_INLINE_PAYLOAD_MAX)
    {
        read_count = BRAM_INLINE_PAYLOAD_MAX;
    }

    uint8_t *dst_bytes = (uint8_t *)out_buf;
    for (uint16_t i = 0; i < read_count; i++)
    {
        dst_bytes[i] = bram_read_byte((uint16_t)(block_offset + 34 + i));
    }

    return true;
}

/**
 * @brief Deletes an existing save file from Backup RAM by clearing its header flag.
 */
bool sub_bram_delete_file(const char *name)
{
    int16_t target_block = sub_bram_find_file(name);
    if (target_block < 0)
    {
        return false;
    }

    uint16_t block_offset = (uint16_t)target_block * BRAM_BLOCK_SIZE;
    /* Mark block as free */
    bram_write_be32(block_offset, BRAM_FLAG_FREE);
    return true;
}

/**
 * @brief Counts the total number of free/available 64-byte blocks in Backup RAM.
 */
uint16_t sub_bram_get_free_blocks(void)
{
    if (!sub_bram_is_formatted())
    {
        return 0;
    }

    uint16_t free_count = 0;
    for (uint16_t b = BRAM_BLOCK_DATA_START; b < BRAM_TOTAL_BLOCKS; b++)
    {
        uint16_t block_offset = b * BRAM_BLOCK_SIZE;
        if (bram_read_be32(block_offset) == BRAM_FLAG_FREE)
        {
            free_count++;
        }
    }
    return free_count;
}
