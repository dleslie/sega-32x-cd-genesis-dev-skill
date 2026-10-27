/**
 * @file sub_iso9660.h
 * @brief Sega CD ISO-9660 Mode 1 Filesystem Parser & Sector Reader.
 *
 * Implements Primary Volume Descriptor (PVD) parsing, directory traversal,
 * path resolution, and sector streaming for loading assets from CD-ROM tracks
 * into Sega CD PRG-RAM and Word RAM.
 *
 * Copyright (c) 2026 Dan Leslie
 * SPDX-License-Identifier: MIT
 */

#ifndef SUB_ISO9660_H
#define SUB_ISO9660_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define ISO_SECTOR_SIZE          2048  /**< Standard Mode 1 ISO-9660 user payload bytes */
#define ISO_PVD_LBA              16    /**< Primary Volume Descriptor logical block address */
#define ISO_STANDARD_ID_LEN      5     /**< Length of "CD001" standard identifier */
#define ISO_MAX_PATH_LEN         128   /**< Maximum supported file path length */
#define ISO_MAX_FILENAME_LEN     32    /**< Maximum supported filename length */

/* File Flag Bitmasks */
#define ISO_FLAG_HIDDEN          0x01  /**< File is marked hidden / existence concealed */
#define ISO_FLAG_DIRECTORY       0x02  /**< Record represents a directory */
#define ISO_FLAG_ASSOCIATED      0x04  /**< Record represents an associated file */
#define ISO_FLAG_RECORD          0x08  /**< Record format structure is specified */
#define ISO_FLAG_PROTECTION      0x10  /**< Permissions / protection specified */
#define ISO_FLAG_MULTI_EXTENT    0x80  /**< Not final extent of a multi-extent file */

#pragma pack(push, 1)

/**
 * @struct IsoDateTime
 * @brief ISO-9660 directory record timestamp structure (7 bytes).
 */
typedef struct {
    uint8_t years_since_1900;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    int8_t  gmt_offset_15min;
} IsoDateTime;

/**
 * @struct IsoDirRecordRaw
 * @brief Raw on-disc layout of an ISO-9660 Directory Record.
 */
typedef struct {
    uint8_t     record_len;            /**< Total length of this directory record in bytes */
    uint8_t     ext_attr_len;          /**< Extended attribute record length */
    uint32_t    extent_lba_le;         /**< Extent start LBA (Little-Endian) */
    uint32_t    extent_lba_be;         /**< Extent start LBA (Big-Endian) */
    uint32_t    data_len_le;           /**< Data payload length in bytes (Little-Endian) */
    uint32_t    data_len_be;           /**< Data payload length in bytes (Big-Endian) */
    IsoDateTime record_time;           /**< Recording timestamp */
    uint8_t     file_flags;            /**< File attribute flags bitfield */
    uint8_t     file_unit_size;        /**< Interleave unit size */
    uint8_t     interleave_gap_size;   /**< Interleave gap size */
    uint16_t    volume_seq_le;         /**< Volume sequence number (Little-Endian) */
    uint16_t    volume_seq_be;         /**< Volume sequence number (Big-Endian) */
    uint8_t     name_len;              /**< Length of file identifier string */
    char        name[1];               /**< File identifier characters (variable length) */
} IsoDirRecordRaw;

/**
 * @struct IsoPvdRaw
 * @brief Raw on-disc layout of an ISO-9660 Primary Volume Descriptor (2048 bytes).
 */
typedef struct {
    uint8_t     type;                  /**< Descriptor type (1 = PVD) */
    char        standard_id[5];        /**< Standard identifier: "CD001" */
    uint8_t     version;               /**< Descriptor version (1) */
    uint8_t     unused0;
    char        system_id[32];         /**< System identifier string */
    char        volume_id[32];         /**< Volume identifier string */
    uint8_t     unused1[8];
    uint32_t    volume_space_size_le;  /**< Total sector count (Little-Endian) */
    uint32_t    volume_space_size_be;  /**< Total sector count (Big-Endian) */
    uint8_t     unused2[32];
    uint16_t    volume_set_size_le;
    uint16_t    volume_set_size_be;
    uint16_t    volume_seq_number_le;
    uint16_t    volume_seq_number_be;
    uint16_t    logical_block_size_le; /**< Logical block size in bytes (Little-Endian) */
    uint16_t    logical_block_size_be; /**< Logical block size in bytes (Big-Endian) */
    uint32_t    path_table_size_le;
    uint32_t    path_table_size_be;
    uint32_t    type_l_path_table_lba;
    uint32_t    opt_type_l_path_table_lba;
    uint32_t    type_m_path_table_lba;
    uint32_t    opt_type_m_path_table_lba;
    uint8_t     root_dir_record[34];   /**< Root directory entry descriptor */
    char        volume_set_id[128];
    char        publisher_id[128];
    char        data_preparer_id[128];
    char        application_id[128];
    char        copyright_file_id[37];
    char        abstract_file_id[37];
    char        bibliographic_file_id[37];
    char        creation_time[17];
    char        modification_time[17];
    char        expiration_time[17];
    char        effective_time[17];
    uint8_t     file_structure_version;
    uint8_t     unused3;
    uint8_t     application_use[512];
    uint8_t     reserved[653];
} IsoPvdRaw;

#pragma pack(pop)

/**
 * @struct IsoFileEntry
 * @brief Cleaned, architecture-neutral representation of an open ISO file or directory.
 */
typedef struct {
    uint32_t lba;                      /**< Starting sector LBA on disc */
    uint32_t size;                     /**< Exact file size in bytes */
    uint8_t  flags;                    /**< ISO_FLAG_* flags */
    char     name[ISO_MAX_FILENAME_LEN]; /**< Sanitized filename without trailing version suffix */
} IsoFileEntry;

/**
 * @brief Function pointer signature for physical/virtual sector reader.
 *
 * @param lba Target sector logical block address.
 * @param dst Pointer to 2048-byte buffer receiving raw sector payload.
 * @return true if read succeeded, false otherwise.
 */
typedef bool (*IsoSectorReaderFn)(uint32_t lba, uint8_t *dst);

/**
 * @brief Initializes the ISO-9660 parser and parses the Primary Volume Descriptor.
 *
 * @param reader Sector reading callback.
 * @return true if valid "CD001" volume was recognized, false otherwise.
 */
bool iso9660_init(IsoSectorReaderFn reader);

/**
 * @brief Queries the root directory entry of the initialized disc.
 *
 * @param out_entry Receives the root directory file descriptor.
 * @return true if root directory was retrieved, false otherwise.
 */
bool iso9660_get_root(IsoFileEntry *out_entry);

/**
 * @brief Locates a file or directory by absolute or relative path.
 * Supports versioned filenames (e.g., "MAPS.BIN;1" or "MAPS.BIN") and path separators ('/' or '\\').
 *
 * @param path File path string to resolve (e.g. "/DATA/TEXTURES.BIN").
 * @param out_entry Receives descriptor if found.
 * @return true if located, false otherwise.
 */
bool iso9660_find_file(const char *path, IsoFileEntry *out_entry);

/**
 * @brief Reads a slice of bytes from an open ISO file entry.
 *
 * @param entry Target file descriptor obtained from iso9660_find_file.
 * @param file_offset Byte offset from beginning of file.
 * @param dst Destination buffer in memory (PRG-RAM or Word RAM).
 * @param count Number of bytes to read.
 * @return Total number of bytes actually read.
 */
uint32_t iso9660_read_file(const IsoFileEntry *entry, uint32_t file_offset, uint8_t *dst, uint32_t count);

/**
 * @brief Returns total volume size in 2048-byte sectors.
 */
uint32_t iso9660_get_volume_size_sectors(void);

#endif /* SUB_ISO9660_H */
