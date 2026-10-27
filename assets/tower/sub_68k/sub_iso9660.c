/**
 * @file sub_iso9660.c
 * @brief Sega CD ISO-9660 Mode 1 Filesystem Parser & Sector Reader Implementation.
 *
 * Provides volume descriptor inspection, directory entry traversal, and
 * direct sector streaming into Sub-CPU PRG-RAM or Word RAM.
 *
 * Copyright (c) 2026 Dan Leslie
 * SPDX-License-Identifier: MIT
 */

#include "sub_iso9660.h"
#include <string.h>

#ifdef __m68k__
/**
 * @brief Standalone memset for bare-metal Sub-CPU execution without standard libc.
 */
void *memset(void *s, int c, size_t n)
{
    uint8_t *p = (uint8_t *)s;
    while (n--)
    {
        *p++ = (uint8_t)c;
    }
    return s;
}

/**
 * @brief Standalone memcpy for bare-metal Sub-CPU execution without standard libc.
 */
void *memcpy(void *dest, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    while (n--)
    {
        *d++ = *s++;
    }
    return dest;
}

/**
 * @brief Standalone memcmp for bare-metal Sub-CPU execution without standard libc.
 */
int memcmp(const void *s1, const void *s2, size_t n)
{
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;
    while (n--)
    {
        if (*p1 != *p2)
        {
            return (int)*p1 - (int)*p2;
        }
        p1++;
        p2++;
    }
    return 0;
}
#endif

static IsoSectorReaderFn s_sector_reader = NULL;
static uint8_t s_sector_buffer[ISO_SECTOR_SIZE];
static IsoFileEntry s_root_dir;
static uint32_t s_volume_sectors = 0;
static bool s_initialized = false;

/**
 * @brief Decodes a 32-bit both-endian integer by reading the big-endian half.
 * Big-endian field is located at byte offset +4 within the 8-byte field.
 */
static inline uint32_t read_u32_both_endian(const uint8_t *p)
{
    /* Always decode big-endian portion at offset 4 for portable cross-platform execution */
    return (((uint32_t)p[4]) << 24) |
           (((uint32_t)p[5]) << 16) |
           (((uint32_t)p[6]) << 8)  |
           ((uint32_t)p[7]);
}

/**
 * @brief Converts an ASCII character to uppercase without locale overhead.
 */
static inline char ascii_toupper(char c)
{
    /* Check for lowercase ASCII range */
    if (c >= 'a' && c <= 'z')
    {
        /* Shift to uppercase ASCII code */
        return (char)(c - ('a' - 'A'));
    }
    return c;
}

/**
 * @brief Compares two filename strings case-insensitively, ignoring trailing ISO version suffix (';1').
 */
static bool iso_name_matches(const char *search_name, const char *record_name, uint8_t record_name_len)
{
    /* Special ISO-9660 root and current directory entries */
    if (record_name_len == 1 && (record_name[0] == 0 || record_name[0] == 1))
    {
        /* Match dot or empty query */
        return (search_name[0] == '.' || search_name[0] == '\0');
    }

    size_t s_idx = 0;
    size_t r_idx = 0;

    while (r_idx < record_name_len)
    {
        char rc = record_name[r_idx];

        /* Terminate comparison on ISO-9660 semicolon version separator if search name does not include it */
        if (rc == ';' && search_name[s_idx] != ';')
        {
            /* Version delimiter reached; verify search name is also terminated */
            break;
        }

        char sc = search_name[s_idx];
        if (sc == '\0')
        {
            /* Search term ended prematurely */
            return false;
        }

        if (ascii_toupper(rc) != ascii_toupper(sc))
        {
            /* Mismatched character */
            return false;
        }

        s_idx++;
        r_idx++;
    }

    /* Confirm search name has also concluded */
    return (search_name[s_idx] == '\0' || search_name[s_idx] == ';');
}

/**
 * @brief Decodes an on-disc directory record into a clean IsoFileEntry struct.
 */
static void parse_dir_record(const IsoDirRecordRaw *raw, IsoFileEntry *out_entry)
{
    /* Read extent LBA from big-endian field (offset +6) */
    const uint8_t *lba_ptr = ((const uint8_t *)raw) + 2;
    out_entry->lba = read_u32_both_endian(lba_ptr);

    /* Read data length from big-endian field (offset +14) */
    const uint8_t *len_ptr = ((const uint8_t *)raw) + 10;
    out_entry->size = read_u32_both_endian(len_ptr);

    out_entry->flags = raw->file_flags;

    /* Extract sanitized filename */
    uint8_t nlen = raw->name_len;
    if (nlen == 1 && raw->name[0] == 0)
    {
        /* Root / Current directory indicator */
        out_entry->name[0] = '.';
        out_entry->name[1] = '\0';
    }
    else if (nlen == 1 && raw->name[0] == 1)
    {
        /* Parent directory indicator */
        out_entry->name[0] = '.';
        out_entry->name[1] = '.';
        out_entry->name[2] = '\0';
    }
    else
    {
        uint8_t copy_len = 0;
        while (copy_len < nlen && copy_len < (ISO_MAX_FILENAME_LEN - 1))
        {
            char c = raw->name[copy_len];
            if (c == ';')
            {
                /* Strip trailing version delimiter for presentation */
                break;
            }
            out_entry->name[copy_len] = c;
            copy_len++;
        }
        out_entry->name[copy_len] = '\0';
    }
}

/**
 * @brief Scans a directory extent for a single matching filename component.
 */
static bool scan_directory_for_component(const IsoFileEntry *dir, const char *component, IsoFileEntry *out_entry)
{
    if (!(dir->flags & ISO_FLAG_DIRECTORY))
    {
        /* Specified parent is not a directory */
        return false;
    }

    uint32_t current_lba = dir->lba;
    uint32_t remaining_bytes = dir->size;

    while (remaining_bytes > 0)
    {
        /* Read next sector of directory listing */
        if (!s_sector_reader(current_lba, s_sector_buffer))
        {
            /* Drive sector read failure */
            return false;
        }

        uint32_t sector_offset = 0;
        uint32_t bytes_in_sector = (remaining_bytes < ISO_SECTOR_SIZE) ? remaining_bytes : ISO_SECTOR_SIZE;

        while (sector_offset < bytes_in_sector)
        {
            const IsoDirRecordRaw *rec = (const IsoDirRecordRaw *)(s_sector_buffer + sector_offset);

            if (rec->record_len == 0)
            {
                /* Record length of 0 represents zero-padding to end of 2048-byte sector */
                break;
            }

            if (iso_name_matches(component, rec->name, rec->name_len))
            {
                /* Matching file or directory found */
                parse_dir_record(rec, out_entry);
                return true;
            }

            sector_offset += rec->record_len;
        }

        current_lba++;
        remaining_bytes -= bytes_in_sector;
    }

    /* Filename not found in this directory */
    return false;
}

bool iso9660_init(IsoSectorReaderFn reader)
{
    if (!reader)
    {
        /* Invalid reader callback provided */
        return false;
    }

    s_sector_reader = reader;

    /* Read Primary Volume Descriptor at sector 16 (0x8000) */
    if (!s_sector_reader(ISO_PVD_LBA, s_sector_buffer))
    {
        /* Unable to read PVD sector */
        s_initialized = false;
        return false;
    }

    const IsoPvdRaw *pvd = (const IsoPvdRaw *)s_sector_buffer;

    /* Verify standard ISO-9660 descriptor identifier ("CD001" with Type = 1) */
    if (pvd->type != 1 || memcmp(pvd->standard_id, "CD001", ISO_STANDARD_ID_LEN) != 0)
    {
        /* Signature check failed: Not a valid ISO-9660 volume */
        s_initialized = false;
        return false;
    }

    /* Decode total volume sector capacity */
    const uint8_t *vol_size_ptr = ((const uint8_t *)pvd) + 80;
    s_volume_sectors = read_u32_both_endian(vol_size_ptr);

    /* Parse root directory record embedded within PVD */
    const IsoDirRecordRaw *root_rec = (const IsoDirRecordRaw *)pvd->root_dir_record;
    parse_dir_record(root_rec, &s_root_dir);
    s_root_dir.flags |= ISO_FLAG_DIRECTORY;

    s_initialized = true;
    return true;
}

bool iso9660_get_root(IsoFileEntry *out_entry)
{
    if (!s_initialized || !out_entry)
    {
        /* Subsystem uninitialized or destination null */
        return false;
    }

    *out_entry = s_root_dir;
    return true;
}

bool iso9660_find_file(const char *path, IsoFileEntry *out_entry)
{
    if (!s_initialized || !path || !out_entry)
    {
        /* Preconditions not met */
        return false;
    }

    /* Skip leading directory separators */
    const char *curr = path;
    while (*curr == '/' || *curr == '\\')
    {
        curr++;
    }

    /* If path is empty, return the root directory descriptor */
    if (*curr == '\0')
    {
        *out_entry = s_root_dir;
        return true;
    }

    IsoFileEntry current_dir = s_root_dir;
    char component[ISO_MAX_FILENAME_LEN];

    while (*curr != '\0')
    {
        /* Extract next path segment */
        size_t c_idx = 0;
        while (*curr != '\0' && *curr != '/' && *curr != '\\' && c_idx < (ISO_MAX_FILENAME_LEN - 1))
        {
            component[c_idx++] = *curr++;
        }
        component[c_idx] = '\0';

        /* Skip trailing consecutive slashes */
        while (*curr == '/' || *curr == '\\')
        {
            curr++;
        }

        /* Search current directory for this component */
        IsoFileEntry found_entry;
        if (!scan_directory_for_component(&current_dir, component, &found_entry))
        {
            /* Path resolution failed at this component */
            return false;
        }

        if (*curr == '\0')
        {
            /* Final target component located */
            *out_entry = found_entry;
            return true;
        }

        /* Must be an intermediate directory to continue traversing */
        if (!(found_entry.flags & ISO_FLAG_DIRECTORY))
        {
            /* Component in middle of path is not a directory */
            return false;
        }

        current_dir = found_entry;
    }

    return false;
}

uint32_t iso9660_read_file(const IsoFileEntry *entry, uint32_t file_offset, uint8_t *dst, uint32_t count)
{
    if (!s_initialized || !entry || !dst || count == 0)
    {
        /* Invalid parameters */
        return 0;
    }

    if (file_offset >= entry->size)
    {
        /* Offset is beyond end of file */
        return 0;
    }

    /* Clamp read length to available file payload */
    uint32_t available = entry->size - file_offset;
    uint32_t to_read = (count < available) ? count : available;

    uint32_t start_sector_index = file_offset / ISO_SECTOR_SIZE;
    uint32_t sector_byte_offset = file_offset % ISO_SECTOR_SIZE;

    uint32_t current_lba = entry->lba + start_sector_index;
    uint32_t bytes_copied = 0;

    while (bytes_copied < to_read)
    {
        /* Read sector into temporary cache */
        if (!s_sector_reader(current_lba, s_sector_buffer))
        {
            /* Read failed prematurely */
            break;
        }

        uint32_t chunk = ISO_SECTOR_SIZE - sector_byte_offset;
        uint32_t needed = to_read - bytes_copied;
        if (chunk > needed)
        {
            chunk = needed;
        }

        /* Transfer byte chunk to target memory buffer */
        memcpy(dst + bytes_copied, s_sector_buffer + sector_byte_offset, chunk);

        bytes_copied += chunk;
        current_lba++;
        sector_byte_offset = 0; /* Subsequent sectors always start at byte 0 */
    }

    return bytes_copied;
}

uint32_t iso9660_get_volume_size_sectors(void)
{
    return s_volume_sectors;
}
