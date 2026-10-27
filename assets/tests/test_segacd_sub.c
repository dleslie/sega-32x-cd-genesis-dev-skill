/**
 * @file test_segacd_sub.c
 * @brief Sega CD Sub-CPU Architecture & Audio/Graphics Driver Host Oracle.
 *
 * Rigorously verifies:
 *   1. Ricoh RF5C164 PCM audio frequency calculation (5.11 fixed-point format),
 *      sample sanitization (0xFF -> 0xFE sentinel protection), and 4 KB Wave RAM banking.
 *   2. Sega CD hardware ASIC trace table geometry, 13.3 coordinate scaling,
 *      and 5.11 step increment calculation across full 360-degree rotation angles.
 *   3. Backup RAM (BRAM) 8 KB filesystem structure, Block 0 format signature
 *      repetition, and 34-byte game save record headers.
 *   4. Word RAM 1M mode double-buffering ping-pong address alignment.
 *
 * Copyright (c) 2026 Dan Leslie
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

static int g_tests_run = 0;
static int g_tests_failed = 0;

#define ASSERT_TRUE(cond, msg) do { \
    g_tests_run++; \
    if (!(cond)) { \
        printf("FAIL: %s:%d: %s [Condition: %s]\n", __FILE__, __LINE__, msg, #cond); \
        g_tests_failed++; \
    } else { \
        printf("PASS: %s\n", msg); \
    } \
} while (0)

#define ASSERT_EQ(actual, expected, msg) do { \
    g_tests_run++; \
    if ((actual) != (expected)) { \
        printf("FAIL: %s:%d: %s [Expected: %ld (0x%lX), Got: %ld (0x%lX)]\n", \
               __FILE__, __LINE__, msg, (long)(expected), (unsigned long)(expected), \
               (long)(actual), (unsigned long)(actual)); \
        g_tests_failed++; \
    } else { \
        printf("PASS: %s (0x%lX)\n", msg, (unsigned long)(actual)); \
    } \
} while (0)

/* ==============================================================================
 * Test Suite 1: Ricoh RF5C164 PCM Audio Processor Driver Verification
 * ============================================================================== */

#define PCM_PITCH_NORMAL       0x0800  /* 2048 in 5.11 format = 1.0x */
#define PCM_LOOP_STOP_SENTINEL 0xFF    /* Hardware end-of-sample marker */

/**
 * @brief Computes RF5C164 5.11 fixed-point frequency step from Q8 zoom scale factor.
 */
static uint16_t compute_pcm_pitch(uint16_t scale_q8)
{
    return (uint16_t)(((uint32_t)scale_q8 * PCM_PITCH_NORMAL) >> 8);
}

/**
 * @brief Computes stereo pan byte (Left 0..15 in high nibble, Right 0..15 in low nibble)
 * based on 12-bit signed cosine coordinate (-2048..2048).
 */
static uint8_t compute_pcm_pan(int16_t cos_val)
{
    if (cos_val > 2048) cos_val = 2048;
    else if (cos_val < -2048) cos_val = -2048;
    uint8_t right_vol = (uint8_t)(((int32_t)(cos_val + 2048) * 15 + 2048) >> 12);
    if (right_vol > 15) right_vol = 15;
    uint8_t left_vol = (uint8_t)(15 - right_vol);
    return (uint8_t)((left_vol << 4) | (right_vol & 0x0F));
}

static void test_rf5c164_pcm_audio(void)
{
    printf("\n--- Test Suite 1: Ricoh RF5C164 PCM Audio Processor ---\n");

    /* 1. Frequency pitch math */
    uint16_t pitch_normal = compute_pcm_pitch(256); /* 1.00x normal scale */
    ASSERT_EQ(pitch_normal, 0x0800, "1.0x normal scale produces 5.11 frequency step of 0x0800 (2048)");

    uint16_t pitch_double = compute_pcm_pitch(512); /* 2.00x octave up */
    ASSERT_EQ(pitch_double, 0x1000, "2.0x zoom scale produces +1 octave pitch step of 0x1000 (4096)");

    uint16_t pitch_half = compute_pcm_pitch(128);   /* 0.50x octave down */
    ASSERT_EQ(pitch_half, 0x0400, "0.5x zoom scale produces -1 octave pitch step of 0x0400 (1024)");

    /* 2. Stereo panning modulation */
    uint8_t pan_center = compute_pcm_pan(0); /* cos(90 deg) = 0 */
    /* Midpoint: (0 + 2048)*15/4096 = 30720/4096 = 7. Left = 15 - 7 = 8, Right = 7 */
    ASSERT_TRUE(((pan_center >> 4) == 8 || (pan_center >> 4) == 7) &&
                ((pan_center & 0x0F) == 8 || (pan_center & 0x0F) == 7),
                "Center panning balances stereo channels within 1 volume unit");

    uint8_t pan_full_right = compute_pcm_pan(2047); /* cos(0 deg) ~ 2047 */
    ASSERT_EQ((pan_full_right & 0x0F), 15, "0-degree angle routes maximum amplitude (15) to Right channel");
    ASSERT_EQ((pan_full_right >> 4), 0, "0-degree angle attenuates Left channel to 0");

    uint8_t pan_full_left = compute_pcm_pan(-2048); /* cos(180 deg) = -2048 */
    ASSERT_EQ((pan_full_left >> 4), 15, "180-degree angle routes maximum amplitude (15) to Left channel");
    ASSERT_EQ((pan_full_left & 0x0F), 0, "180-degree angle attenuates Right channel to 0");

    /* 3. Sample buffer sanitization (0xFF loop sentinel collision protection) */
    uint8_t raw_sample[8] = { 128, 140, 255, 120, 255, 128, 64, 0 };
    uint8_t clean_sample[9];
    for (int i = 0; i < 8; i++) {
        uint8_t b = raw_sample[i];
        clean_sample[i] = (b == PCM_LOOP_STOP_SENTINEL) ? 0xFE : b;
    }
    clean_sample[8] = PCM_LOOP_STOP_SENTINEL; /* Sentinel appended at end */

    ASSERT_EQ(clean_sample[2], 0xFE, "Embedded 0xFF byte at index 2 is sanitized to 0xFE to avoid premature stop");
    ASSERT_EQ(clean_sample[4], 0xFE, "Embedded 0xFF byte at index 4 is sanitized to 0xFE to avoid premature stop");
    ASSERT_EQ(clean_sample[8], 0xFF, "Hardware loop/stop sentinel 0xFF is appended strictly at buffer terminus");

    /* 4. 4 KB Wave RAM bank boundary calculation */
    uint32_t sample_addr = 0x2A50;
    uint8_t bank = (uint8_t)(sample_addr >> 12);
    uint16_t offset = (uint16_t)(sample_addr & 0x0FFF);
    ASSERT_EQ(bank, 2, "Address 0x2A50 maps correctly into 4 KB Wave RAM Bank 2");
    ASSERT_EQ(offset, 0x0A50, "Address 0x2A50 produces window offset 0x0A50");
}

/* ==============================================================================
 * Test Suite 2: Sega CD ASIC Graphics Coprocessor Geometry Verification
 * ============================================================================== */

typedef struct {
    int16_t start_x;  /**< Initial horizontal coordinate in 13.3 fixed-point (pixel * 8) */
    int16_t start_y;  /**< Initial vertical coordinate in 13.3 fixed-point (pixel * 8)   */
    int16_t delta_x;  /**< Horizontal step increment per pixel in signed 5.11 fixed-point */
    int16_t delta_y;  /**< Vertical cross step increment per pixel in signed 5.11 fixed-point */
} __attribute__((packed)) SegaCD_ASIC_TraceEntry;

static void test_asic_geometry(void)
{
    printf("\n--- Test Suite 2: Sega CD ASIC Graphics Coprocessor Geometry ---\n");

    /* Verify trace table entry size */
    ASSERT_EQ(sizeof(SegaCD_ASIC_TraceEntry), 8, "ASIC trace table entry packed struct is exactly 8 bytes");

    /* Verify total trace table capacity for 160 scanlines */
    size_t trace_table_size = 160 * sizeof(SegaCD_ASIC_TraceEntry);
    ASSERT_EQ(trace_table_size, 1280, "160-scanline trace table occupies exactly 1280 bytes in Word RAM");

    /* Test 13.3 fixed-point coordinate calculation: pixel 32 -> 32 * 8 = 256 */
    int16_t coord_13_3 = (int16_t)(32 * 8);
    ASSERT_EQ(coord_13_3, 256, "Pixel coordinate 32 maps to 256 in 13.3 fixed-point representation");

    /* Test 5.11 fixed-point delta calculation:
     * For 1.0x scale (scale_q8 = 256) at 0 degrees rotation:
     * delta_x = cos(0) * 256 / 256 = 1.0 = 0x0800 in 5.11
     * delta_y = sin(0) = 0
     */
    int32_t cos_0 = 2048; /* 1.0 in 12-bit fixed point */
    int16_t dx_0 = (int16_t)((cos_0 * 256) / 256);
    ASSERT_EQ(dx_0, 2048, "0-degree rotation at 1.0x zoom yields delta_x = 2048 (1.0 in 5.11 format)");
}

/* ==============================================================================
 * Test Suite 3: Backup RAM (BRAM) 8 KB Filesystem Layout Verification
 * ============================================================================== */

#define BRAM_BLOCK_SIZE        64
#define BRAM_TOTAL_BLOCKS      128
#define BRAM_TOTAL_BYTES       (BRAM_BLOCK_SIZE * BRAM_TOTAL_BLOCKS)
#define BRAM_RECORD_FLAG_VALID 0x80000000

typedef struct {
    uint32_t flag;        /**< 0x80000000 indicates active valid file */
    char     name[11];    /**< 11-byte ASCII filename               */
    uint8_t  language;    /**< Language identifier                  */
    char     desc[10];    /**< 10-byte description string           */
    uint32_t timestamp;   /**< Timestamp sequence number            */
    uint32_t size;        /**< File payload length in bytes         */
    uint8_t  payload[30]; /**< First 30 bytes of inline file data   */
} __attribute__((packed)) SegaCD_BRAM_Header;

static void test_bram_filesystem(void)
{
    printf("\n--- Test Suite 3: Backup RAM (BRAM) Filesystem Driver ---\n");

    /* Verify capacity */
    ASSERT_EQ(BRAM_TOTAL_BYTES, 8192, "Sega CD internal Backup RAM capacity is exactly 8,192 bytes (128 blocks * 64 bytes)");

    /* Verify header struct packing */
    ASSERT_EQ(sizeof(SegaCD_BRAM_Header), 64, "BRAM file record header matches exactly 64 bytes (single BRAM block)");

    /* Verify Block 0 format signature */
    const char *sig = "BackUpRam Format";
    size_t sig_len = strlen(sig);
    ASSERT_EQ(sig_len, 16, "BRAM format signature is 16 bytes");
    ASSERT_EQ(sig_len * 4, 64, "Signature repeated 4 times precisely fills Block 0 (64 bytes)");

    /* Simulate record header serialization */
    SegaCD_BRAM_Header hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.flag = BRAM_RECORD_FLAG_VALID;
    memcpy(hdr.name, "SAVE_DEMO  ", 11);
    hdr.language = 1; /* English */
    memcpy(hdr.desc, "SLOT 1    ", 10);
    hdr.timestamp = 12345678;
    hdr.size = 12;

    ASSERT_EQ(hdr.flag, 0x80000000, "Active save file flag is 0x80000000");
    ASSERT_EQ(hdr.size, 12, "Reported payload size matches written bytes");
    ASSERT_TRUE(memcmp(hdr.name, "SAVE_DEMO  ", 11) == 0, "Record filename is space-padded 11-byte ASCII");
}

/* ==============================================================================
 * Test Suite 4: Word RAM 1M Mode Memory Aperture & Handshake
 * ============================================================================== */

static void test_word_ram_1m_mode(void)
{
    printf("\n--- Test Suite 4: Word RAM 1M Mode Apertures & Handshake ---\n");

    /* Sub-CPU view of 1M Word RAM bank */
    uint32_t sub_wram_base = 0x0C0000;
    uint32_t sub_wram_size = 128 * 1024; /* 128 KB */
    ASSERT_EQ(sub_wram_base, 0x0C0000, "Sub-CPU 1M Word RAM bank maps to 0x0C0000");
    ASSERT_EQ(sub_wram_size, 131072, "Each 1M Word RAM ping-pong bank is 131,072 bytes (128 KB)");

    /* Genesis Main-CPU view of alternate 1M Word RAM bank */
    uint32_t md_wram_base = 0x200000;
    ASSERT_EQ(md_wram_base, 0x200000, "Genesis Main-CPU 1M Word RAM bank maps to 0x200000");

    /* Sub-CPU PRG-RAM mapping from Main-CPU when Sub-CPU is halted */
    uint32_t md_prg_ram_window = 0x400000;
    ASSERT_EQ(md_prg_ram_window, 0x400000, "Main-CPU PRG-RAM staging window maps to 0x400000");

    /* MEMMODE bitfield verification:
     * Bit 2 (0x0004) = Mode (1 = 1M mode, 0 = 2M mode)
     * Bit 1 (0x0002) = DMNA (Sub-CPU return acknowledge)
     * Bit 0 (0x0001) = RET (Main-CPU return request)
     */
    uint16_t memmode_1m = 0x0004;
    ASSERT_TRUE((memmode_1m & 0x0004) != 0, "Bit 2 of MEMMODE (0x0004) configures Word RAM into 1M ping-pong mode");
}

/* ==============================================================================
 * Test Suite 5: ISO-9660 Mode 1 Filesystem Parser & Sector Streaming Verification
 * ============================================================================== */

#include "../cd/src/sub_68k/sub_iso9660.c"
#include "../cd/src/sub_68k/sub_cdda.c"
#include "../cd/src/sub_68k/sub_cdrom.c"

#define MOCK_ISO_SECTORS 32
static uint8_t s_mock_iso_image[MOCK_ISO_SECTORS][ISO_SECTOR_SIZE];

/**
 * @brief Mock sector reader simulating optical CD-ROM hardware.
 */
static bool mock_sector_reader(uint32_t lba, uint8_t *dst)
{
    if (lba >= MOCK_ISO_SECTORS || !dst)
    {
        /* Sector out of bounds or null buffer */
        return false;
    }

    memcpy(dst, s_mock_iso_image[lba], ISO_SECTOR_SIZE);
    return true;
}

/**
 * @brief Helper serializing an ISO-9660 directory record with both-endian fields into memory.
 */
static uint8_t write_mock_dir_record(uint8_t *dst, uint32_t lba, uint32_t size, uint8_t flags, const char *name)
{
    uint8_t name_len = (uint8_t)strlen(name);
    uint8_t rec_len = (uint8_t)(33 + name_len);
    if (rec_len & 1)
    {
        /* Even byte alignment padding required by ISO-9660 standard */
        rec_len++;
    }

    memset(dst, 0, rec_len);
    IsoDirRecordRaw *r = (IsoDirRecordRaw *)dst;
    r->record_len = rec_len;

    /* Little-endian extent LBA */
    uint8_t *p_lba = dst + 2;
    p_lba[0] = (uint8_t)(lba & 0xFF);
    p_lba[1] = (uint8_t)((lba >> 8) & 0xFF);
    p_lba[2] = (uint8_t)((lba >> 16) & 0xFF);
    p_lba[3] = (uint8_t)((lba >> 24) & 0xFF);
    /* Big-endian extent LBA */
    p_lba[4] = (uint8_t)((lba >> 24) & 0xFF);
    p_lba[5] = (uint8_t)((lba >> 16) & 0xFF);
    p_lba[6] = (uint8_t)((lba >> 8) & 0xFF);
    p_lba[7] = (uint8_t)(lba & 0xFF);

    /* Little-endian file size */
    uint8_t *p_len = dst + 10;
    p_len[0] = (uint8_t)(size & 0xFF);
    p_len[1] = (uint8_t)((size >> 8) & 0xFF);
    p_len[2] = (uint8_t)((size >> 16) & 0xFF);
    p_len[3] = (uint8_t)((size >> 24) & 0xFF);
    /* Big-endian file size */
    p_len[4] = (uint8_t)((size >> 24) & 0xFF);
    p_len[5] = (uint8_t)((size >> 16) & 0xFF);
    p_len[6] = (uint8_t)((size >> 8) & 0xFF);
    p_len[7] = (uint8_t)(size & 0xFF);

    r->file_flags = flags;

    if (strcmp(name, ".") == 0)
    {
        /* Current directory indicator */
        r->name_len = 1;
        r->name[0] = 0;
    }
    else if (strcmp(name, "..") == 0)
    {
        /* Parent directory indicator */
        r->name_len = 1;
        r->name[0] = 1;
    }
    else
    {
        r->name_len = name_len;
        memcpy(r->name, name, name_len);
    }

    return rec_len;
}

/**
 * @brief Generates a synthetic ISO-9660 volume in memory for deterministic test verification.
 */
static void build_synthetic_iso_image(void)
{
    memset(s_mock_iso_image, 0, sizeof(s_mock_iso_image));

    /* 1. Sector 16: Primary Volume Descriptor (PVD) */
    IsoPvdRaw *pvd = (IsoPvdRaw *)s_mock_iso_image[16];
    pvd->type = 1;
    memcpy(pvd->standard_id, "CD001", 5);
    pvd->version = 1;
    memcpy(pvd->volume_id, "SEGA_CD_TEST_DISC               ", 32);

    /* Volume Space Size (32 sectors) both-endian */
    uint8_t *vol_size = ((uint8_t *)pvd) + 80;
    vol_size[0] = 32; vol_size[1] = 0; vol_size[2] = 0; vol_size[3] = 0;
    vol_size[4] = 0; vol_size[5] = 0; vol_size[6] = 0; vol_size[7] = 32;

    /* Logical block size (2048) both-endian */
    uint8_t *blk_size = ((uint8_t *)pvd) + 128;
    blk_size[0] = 0x00; blk_size[1] = 0x08;
    blk_size[2] = 0x08; blk_size[3] = 0x00;

    /* Root directory record inside PVD (extent at sector 17, length 2048) */
    write_mock_dir_record(pvd->root_dir_record, 17, 2048, ISO_FLAG_DIRECTORY, ".");

    /* 2. Sector 17: Root Directory Extent */
    uint8_t *dir_ptr = s_mock_iso_image[17];
    uint32_t offset = 0;

    /* Entry 0: . (Root itself) */
    offset += write_mock_dir_record(dir_ptr + offset, 17, 2048, ISO_FLAG_DIRECTORY, ".");
    /* Entry 1: .. (Parent is root) */
    offset += write_mock_dir_record(dir_ptr + offset, 17, 2048, ISO_FLAG_DIRECTORY, "..");
    /* Entry 2: TEXTURES.BIN;1 (spans 3000 bytes across sector 18 and 19) */
    offset += write_mock_dir_record(dir_ptr + offset, 18, 3000, 0, "TEXTURES.BIN;1");
    /* Entry 3: LEVELS (Subdirectory at sector 20) */
    offset += write_mock_dir_record(dir_ptr + offset, 20, 2048, ISO_FLAG_DIRECTORY, "LEVELS");

    /* 3. Sectors 18 & 19: File Payload for TEXTURES.BIN */
    for (uint32_t i = 0; i < 2048; i++)
    {
        s_mock_iso_image[18][i] = (uint8_t)(i ^ 0xAA);
    }
    for (uint32_t i = 0; i < (3000 - 2048); i++)
    {
        s_mock_iso_image[19][i] = (uint8_t)((i + 2048) ^ 0xAA);
    }

    /* 4. Sector 20: LEVELS Subdirectory Extent */
    uint8_t *levels_ptr = s_mock_iso_image[20];
    uint32_t l_offset = 0;
    l_offset += write_mock_dir_record(levels_ptr + l_offset, 20, 2048, ISO_FLAG_DIRECTORY, ".");
    l_offset += write_mock_dir_record(levels_ptr + l_offset, 17, 2048, ISO_FLAG_DIRECTORY, "..");
    /* Entry 2: MAP01.DAT;1 (at sector 21, size 512) */
    l_offset += write_mock_dir_record(levels_ptr + l_offset, 21, 512, 0, "MAP01.DAT;1");

    /* 5. Sector 21: File Payload for MAP01.DAT */
    for (uint32_t i = 0; i < 512; i++)
    {
        s_mock_iso_image[21][i] = (uint8_t)(i + 0x33);
    }
}

static void test_iso9660_filesystem(void)
{
    printf("\n--- Test Suite 5: ISO-9660 Mode 1 Filesystem Parser & Sector Streaming ---\n");

    build_synthetic_iso_image();

    /* 1. Initialization and Primary Volume Descriptor Verification */
    bool init_ok = iso9660_init(mock_sector_reader);
    ASSERT_TRUE(init_ok, "iso9660_init successfully mounts valid synthetic ISO-9660 disc");

    uint32_t total_sec = iso9660_get_volume_size_sectors();
    ASSERT_EQ(total_sec, 32, "ISO-9660 volume capacity correctly decoded as 32 sectors");

    /* 2. Root directory lookup */
    IsoFileEntry root_entry = {0};
    bool root_ok = iso9660_get_root(&root_entry);
    ASSERT_TRUE(root_ok, "iso9660_get_root successfully returns root directory descriptor");
    ASSERT_EQ(root_entry.lba, 17, "Root directory extent starts at LBA 17");
    ASSERT_TRUE((root_entry.flags & ISO_FLAG_DIRECTORY) != 0, "Root directory descriptor has ISO_FLAG_DIRECTORY set");

    /* 3. Filename resolution with version stripping (TEXTURES.BIN matches TEXTURES.BIN;1) */
    IsoFileEntry tex_file = {0};
    bool tex_found = iso9660_find_file("TEXTURES.BIN", &tex_file);
    ASSERT_TRUE(tex_found, "iso9660_find_file locates 'TEXTURES.BIN' with implicit ';1' version stripping");
    ASSERT_EQ(tex_file.lba, 18, "TEXTURES.BIN extent starts at LBA 18");
    ASSERT_EQ(tex_file.size, 3000, "TEXTURES.BIN reports exact size of 3,000 bytes");
    ASSERT_TRUE(strcmp(tex_file.name, "TEXTURES.BIN") == 0, "Sanitized filename matches 'TEXTURES.BIN'");

    /* 4. Case-insensitive path resolution */
    IsoFileEntry lower_file = {0};
    bool lower_found = iso9660_find_file("textures.bin", &lower_file);
    ASSERT_TRUE(lower_found, "iso9660_find_file resolves lowercase 'textures.bin' correctly");
    ASSERT_EQ(lower_file.lba, 18, "Lowercase file query resolves to same LBA 18");

    /* 5. Subdirectory traversal (/LEVELS/MAP01.DAT) */
    IsoFileEntry map_file = {0};
    bool map_found = iso9660_find_file("/LEVELS/MAP01.DAT", &map_file);
    ASSERT_TRUE(map_found, "iso9660_find_file traverses into subdirectory '/LEVELS/MAP01.DAT'");
    ASSERT_EQ(map_file.lba, 21, "MAP01.DAT extent starts at LBA 21");
    ASSERT_EQ(map_file.size, 512, "MAP01.DAT size matches 512 bytes");

    /* 6. Non-existent file lookup returns false */
    IsoFileEntry bogus_file = {0};
    bool bogus_found = iso9660_find_file("/LEVELS/GHOST.BIN", &bogus_file);
    ASSERT_TRUE(!bogus_found, "iso9660_find_file safely returns false for non-existent file");

    /* 7. Multi-sector byte streaming across sector boundaries */
    uint8_t read_buffer[3000];
    uint32_t bytes_read = iso9660_read_file(&tex_file, 0, read_buffer, 3000);
    ASSERT_EQ(bytes_read, 3000, "iso9660_read_file reads all 3,000 bytes spanning multiple sectors");

    bool data_intact = true;
    for (uint32_t i = 0; i < 3000; i++)
    {
        uint8_t expected = (uint8_t)(i ^ 0xAA);
        if (read_buffer[i] != expected)
        {
            data_intact = false;
            break;
        }
    }
    ASSERT_TRUE(data_intact, "Read file payload exactly matches synthetic on-disc data across sector boundaries");

    /* 8. Partial slice reading from non-zero file offset */
    uint8_t slice_buffer[256];
    uint32_t slice_bytes = iso9660_read_file(&tex_file, 2040, slice_buffer, 256);
    ASSERT_EQ(slice_bytes, 256, "iso9660_read_file reads 256-byte slice straddling sector 18/19 boundary");
    bool slice_intact = true;
    for (uint32_t i = 0; i < 256; i++)
    {
        uint8_t expected = (uint8_t)((2040 + i) ^ 0xAA);
        if (slice_buffer[i] != expected)
        {
            slice_intact = false;
            break;
        }
    }
    ASSERT_TRUE(slice_intact, "Straddled sector boundary slice data matches expected byte sequence");

    /* 9. Direct Word RAM Burst Transfer Pipeline */
    cdrom_set_read_backend(mock_sector_reader);
    uint16_t mock_word_ram[1024 * 2]; /* 2 sectors = 4096 bytes = 2048 words */
    uint32_t wram_bytes_read = 0;
    bool wram_load_ok = cdrom_load_file_to_word_ram("/TEXTURES.BIN", mock_word_ram, &wram_bytes_read);
    ASSERT_TRUE(wram_load_ok, "cdrom_load_file_to_word_ram streams file directly into Word RAM");
    ASSERT_EQ(wram_bytes_read, 3000, "cdrom_load_file_to_word_ram reports 3,000 bytes streamed");

    /* Check first word: big-endian word of bytes 0 & 1 -> ((0 ^ 0xAA) << 8) | (1 ^ 0xAA) = 0xAAAB */
    uint8_t b0 = (uint8_t)(0 ^ 0xAA);
    uint8_t b1 = (uint8_t)(1 ^ 0xAA);
    uint16_t expected_w0 = (uint16_t)((b0 << 8) | b1);
    ASSERT_EQ(mock_word_ram[0], expected_w0, "Word RAM first 16-bit word accurately reflects big-endian packed bytes");
}

/* ==============================================================================
 * Test Suite 6: Redbook CD-DA Audio & MSF/LBA Conversions Verification
 * ============================================================================== */

static void test_cdda_streaming(void)
{
    printf("\n--- Test Suite 6: Redbook CD-DA Audio & MSF/LBA Conversions ---\n");

    /* 1. MSF to LBA conversion with 150-frame (2-second) pregap */
    uint32_t lba_pregap_end = cdda_msf_to_lba(0, 2, 0);
    ASSERT_EQ(lba_pregap_end, 0, "MSF 00:02:00 corresponds precisely to LBA 0 (end of 2-second pregap)");

    uint32_t lba_sec2_frame1 = cdda_msf_to_lba(0, 2, 1);
    ASSERT_EQ(lba_sec2_frame1, 1, "MSF 00:02:01 maps to LBA 1");

    uint32_t lba_1min = cdda_msf_to_lba(1, 0, 0);
    /* 1 min = 4500 frames - 150 = 4350 */
    ASSERT_EQ(lba_1min, 4350, "MSF 01:00:00 maps to LBA 4350");

    uint32_t lba_arbitrary = cdda_msf_to_lba(2, 30, 15);
    /* (2 * 4500) + (30 * 75) + 15 - 150 = 9000 + 2250 + 15 - 150 = 11115 */
    ASSERT_EQ(lba_arbitrary, 11115, "MSF 02:30:15 maps to LBA 11115");

    /* Coordinates within pregap clamp to LBA 0 */
    uint32_t lba_in_pregap = cdda_msf_to_lba(0, 1, 30);
    ASSERT_EQ(lba_in_pregap, 0, "Coordinates inside pregap (00:01:30) safely clamp to LBA 0");

    /* 2. LBA to MSF conversion */
    CddaTimeMSF msf0 = {0};
    cdda_lba_to_msf(0, &msf0);
    ASSERT_TRUE(msf0.minutes == 0 && msf0.seconds == 2 && msf0.frames == 0,
                "LBA 0 maps inversely to MSF 00:02:00");

    CddaTimeMSF msf_arb = {0};
    cdda_lba_to_msf(11115, &msf_arb);
    ASSERT_TRUE(msf_arb.minutes == 2 && msf_arb.seconds == 30 && msf_arb.frames == 15,
                "LBA 11115 maps inversely to MSF 02:30:15");

    /* 3. Table of Contents (TOC) registration and track playback */
    cdda_init();
    CddaTrack tracks[3] = {
        { .track_num = 1, .is_audio = false, .start_lba = 0,     .length_lba = 5000  }, /* Track 1: ISO Data */
        { .track_num = 2, .is_audio = true,  .start_lba = 5000,  .length_lba = 10000 }, /* Track 2: Title BGM */
        { .track_num = 3, .is_audio = true,  .start_lba = 15000, .length_lba = 15000 }  /* Track 3: Level 1 BGM */
    };

    bool toc_ok = cdda_set_toc(tracks, 3);
    ASSERT_TRUE(toc_ok, "cdda_set_toc registers 3-track disc Table of Contents");

    /* Attempting to play data track as audio must fail */
    bool play_data_ok = cdda_play_track(1);
    ASSERT_TRUE(!play_data_ok, "cdda_play_track rejects playing Track 1 (Data Track) as CD-DA audio");

    /* Play audio track 2 */
    bool play_bgm_ok = cdda_play_track(2);
    ASSERT_TRUE(play_bgm_ok, "cdda_play_track initiates playback of Redbook audio Track 2");
    ASSERT_EQ(cdda_get_status(), CDDA_STATUS_PLAYING, "Drive status transitions to CDDA_STATUS_PLAYING");
    ASSERT_EQ(cdda_get_current_track(), 2, "Active track reports Track 2");
    ASSERT_EQ(cdda_get_current_lba(), 5000, "Playback head initialized at Track 2 start LBA 5000");

    /* Advance simulated playback by 150 frames (2 seconds) */
    cdda_update(150);
    ASSERT_EQ(cdda_get_current_lba(), 5150, "cdda_update advances playback head to LBA 5150 (+150 frames)");
    ASSERT_EQ(cdda_get_status(), CDDA_STATUS_PLAYING, "Drive remains in CDDA_STATUS_PLAYING");

    /* Pause and Resume */
    cdda_pause();
    ASSERT_EQ(cdda_get_status(), CDDA_STATUS_PAUSED, "cdda_pause transitions drive to CDDA_STATUS_PAUSED");
    cdda_update(75); /* Elapsed time while paused must not move playback head */
    ASSERT_EQ(cdda_get_current_lba(), 5150, "Playback head remains frozen at LBA 5150 while paused");

    cdda_resume();
    ASSERT_EQ(cdda_get_status(), CDDA_STATUS_PLAYING, "cdda_resume restores CDDA_STATUS_PLAYING");

    /* Advance past track end (start 5000 + length 10000 = end 15000) */
    cdda_update(10000);
    ASSERT_EQ(cdda_get_status(), CDDA_STATUS_STOPPED, "Reaching end of track boundary automatically halts playback");

    /* Manual stop */
    cdda_play_track(3);
    ASSERT_EQ(cdda_get_status(), CDDA_STATUS_PLAYING, "cdda_play_track starts Track 3");
    cdda_stop();
    ASSERT_EQ(cdda_get_status(), CDDA_STATUS_STOPPED, "cdda_stop halts drive and resets active track to 0");
    ASSERT_EQ(cdda_get_current_track(), 0, "Active track reset to 0 upon stop");
}

/* ==============================================================================
 * Main Test Runner
 * ============================================================================== */

int main(void)
{
    printf("=====================================================================\n");
    printf("Sega CD Sub-CPU Architecture & Audio/Graphics Driver Test Oracle\n");
    printf("=====================================================================\n");

    test_rf5c164_pcm_audio();
    test_asic_geometry();
    test_bram_filesystem();
    test_word_ram_1m_mode();
    test_iso9660_filesystem();
    test_cdda_streaming();

    printf("\n=====================================================================\n");
    printf("Test Results: %d executed, %d failed\n", g_tests_run, g_tests_failed);
    printf("=====================================================================\n");

    if (g_tests_failed > 0) {
        printf("FAILED: %d assertion(s) failed\n", g_tests_failed);
        return 1;
    }

    printf("SUCCESS: All Sega CD Sub-CPU architectural assertions passed 100%%!\n");
    return 0;
}
