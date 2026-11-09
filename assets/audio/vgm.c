#include "vgm.h"
#include <string.h>

/**
 * @brief Reads a 32-bit unsigned integer in little-endian byte order.
 *
 * VGM binary format headers use little-endian encoding on disk regardless of host CPU endianness.
 *
 * @param p Pointer to unaligned byte array.
 * @return 32-bit value in host representation.
 */
static uint32_t read_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/**
 * @brief Reads a 16-bit unsigned integer in little-endian byte order.
 *
 * @param p Pointer to unaligned byte array.
 * @return 16-bit value in host representation.
 */
static uint16_t read_u16_le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint32_t)p[1] << 8);
}

/**
 * @brief Parses and validates a standard VGM 1.50+ audio file header.
 *
 * Checks for "Vgm " signature, verifies minimum file size, extracts sound chip clocks
 * (YM2612 / SN76489), and calculates absolute audio data start offset.
 *
 * @param buf Raw VGM binary file buffer.
 * @param size Buffer size in bytes.
 * @param out_hdr Pointer to header structure to populate.
 * @return 0 on success, negative error code on failure (-1: invalid args, -2: bad magic, -3: data past EOF).
 */
int d32_vgm_parse_header(const uint8_t *buf, size_t size, D32VgmHeader *out_hdr)
{
    /* Require non-null pointers and at least standard 64-byte minimal VGM header size. */
    if (!buf || size < 64 || !out_hdr) {
        return -1;
    }

    /* Verify 4-byte ASCII signature "Vgm " (0x206D6756 little-endian). */
    if (buf[0] != 'V' || buf[1] != 'g' || buf[2] != 'm' || buf[3] != ' ') {
        return -2;
    }

    out_hdr->magic = read_u32_le(buf + 0x00);
    out_hdr->eof_offset = read_u32_le(buf + 0x04);
    out_hdr->version = read_u32_le(buf + 0x08);
    out_hdr->sn76489_clock = read_u32_le(buf + 0x0C);
    out_hdr->ym2413_clock = read_u32_le(buf + 0x10);
    out_hdr->gd3_offset = read_u32_le(buf + 0x14);
    out_hdr->total_samples = read_u32_le(buf + 0x18);
    out_hdr->loop_offset = read_u32_le(buf + 0x1C);
    out_hdr->loop_samples = read_u32_le(buf + 0x20);
    out_hdr->rate = read_u32_le(buf + 0x24);
    out_hdr->ym2612_clock = read_u32_le(buf + 0x2C);

    /*
     * Calculate absolute data offset.
     * In VGM 1.50+, relative data offset is at 0x34. If zero or missing, standard data begins at 0x40.
     */
    uint32_t rel_data = (size >= 0x38) ? read_u32_le(buf + 0x34) : 0;
    out_hdr->data_offset = (rel_data != 0) ? (0x34 + rel_data) : 0x40;

    /* Reject file if data stream start extends past total file size. */
    if (out_hdr->data_offset >= size) {
        return -3;
    }

    return 0;
}

/**
 * @brief Initializes a VGM stream player state for playback.
 *
 * Parses header metadata, positions read cursor to stream start, and resolves loop point.
 *
 * @param player Pointer to player state structure.
 * @param buf Raw VGM binary file buffer.
 * @param size Buffer size in bytes.
 * @return 0 on success, negative error code on invalid header.
 */
int d32_vgm_player_init(D32VgmPlayer *player, const uint8_t *buf, size_t size)
{
    /* Parameter sanity check. */
    if (!player || !buf || size < 64) {
        return -1;
    }

    D32VgmHeader hdr;
    /* Reject invalid or malformed headers. */
    if (d32_vgm_parse_header(buf, size, &hdr) != 0) {
        return -2;
    }

    player->data = buf;
    player->size = size;
    player->data_offset = hdr.data_offset;
    player->cur_pos = hdr.data_offset;
    player->wait_samples = 0;
    player->playing = 1;

    /* If file defines a loop offset, compute absolute file position and mark track as loopable. */
    if (hdr.loop_offset != 0) {
        player->loop_offset = 0x1C + hdr.loop_offset;
        player->loopable = (player->loop_offset < size) ? 1 : 0;
    } else {
        /* No loop point specified: default loop offset to data start and disable looping. */
        player->loop_offset = hdr.data_offset;
        player->loopable = 0;
    }

    return 0;
}

/**
 * @brief Steps the VGM playback engine by one 60 Hz frame (735 audio samples at 44.1 kHz).
 *
 * Decodes chip register writes (YM2612 Port 0/1, SN76489 PSG) and dispatches them
 * to the provided hardware write callback until the 735-sample frame budget is exhausted.
 *
 * @param player Pointer to active VGM player state.
 * @param cb Hardware register write dispatch callback.
 * @param user Context pointer passed to callback.
 * @return Number of VGM stream commands processed during this frame.
 */
int d32_vgm_player_tick(D32VgmPlayer *player, d32_vgm_write_cb cb, void *user)
{
    /* Exit early if player is inactive, stopped, or data is missing. */
    if (!player || !player->playing || !player->data) {
        return 0;
    }

    /*
     * If previous wait duration spans entire current frame, deduct 735 samples
     * and remain silent this frame without executing new commands.
     */
    if (player->wait_samples >= D32_VGM_SAMPLES_PER_FRAME_60HZ) {
        player->wait_samples -= D32_VGM_SAMPLES_PER_FRAME_60HZ;
        return 0;
    }

    /* Remaining sample budget to process in this 60 Hz frame. */
    int32_t budget = D32_VGM_SAMPLES_PER_FRAME_60HZ - player->wait_samples;
    player->wait_samples = 0;

    int commands_executed = 0;
    int guard = 512; /* Guard against malformed stream infinite loops. */

    while (guard-- > 0 && player->playing && budget > 0) {
        /* End of data stream reached unexpectedly without 0x66 end marker. */
        if (player->cur_pos >= player->size) {
            player->playing = 0;
            break;
        }

        uint8_t cmd = player->data[player->cur_pos++];

        /* YM2612 FM Port 0 write: register byte followed by data byte. */
        if (cmd == D32_VGM_CMD_YM2612_P0) {
            if (player->cur_pos + 2 > player->size) { player->playing = 0; break; }
            uint8_t reg = player->data[player->cur_pos++];
            uint8_t val = player->data[player->cur_pos++];
            if (cb) { cb(user, 0, reg, val); }
            commands_executed++;
        } else if (cmd == D32_VGM_CMD_YM2612_P1) {
            /* YM2612 FM Port 1 write: register byte followed by data byte. */
            if (player->cur_pos + 2 > player->size) { player->playing = 0; break; }
            uint8_t reg = player->data[player->cur_pos++];
            uint8_t val = player->data[player->cur_pos++];
            if (cb) { cb(user, 1, reg, val); }
            commands_executed++;
        } else if (cmd == D32_VGM_CMD_PSG) {
            /* SN76489 PSG write: single data byte. */
            if (player->cur_pos + 1 > player->size) { player->playing = 0; break; }
            uint8_t val = player->data[player->cur_pos++];
            if (cb) { cb(user, 2, 0, val); }
            commands_executed++;
        } else if (cmd == D32_VGM_CMD_WAIT_60HZ) {
            /* Wait 735 samples (1/60th second). */
            commands_executed++;
            if (735 >= budget) {
                player->wait_samples = 735 - budget;
                budget = 0;
            } else {
                budget -= 735;
            }
        } else if (cmd == D32_VGM_CMD_WAIT_50HZ) {
            /* Wait 882 samples (1/50th second, PAL). */
            commands_executed++;
            if (882 >= budget) {
                player->wait_samples = 882 - budget;
                budget = 0;
            } else {
                budget -= 882;
            }
        } else if (cmd == D32_VGM_CMD_WAIT) {
            /* Arbitrary 16-bit sample wait duration. */
            if (player->cur_pos + 2 > player->size) { player->playing = 0; break; }
            uint16_t w = read_u16_le(player->data + player->cur_pos);
            player->cur_pos += 2;
            commands_executed++;
            if ((int32_t)w >= budget) {
                player->wait_samples = (int32_t)w - budget;
                budget = 0;
            } else {
                budget -= w;
            }
        } else if ((cmd & 0xF0) == 0x70) {
            /* Short sample wait: (n + 1) samples for command 0x7n. */
            int32_t w = (cmd & 0x0F) + 1;
            commands_executed++;
            if (w >= budget) {
                player->wait_samples = w - budget;
                budget = 0;
            } else {
                budget -= w;
            }
        } else if (cmd == D32_VGM_CMD_END) {
            /* End of sound data (0x66): rewind to loop offset if loopable, else stop playback. */
            commands_executed++;
            if (player->loopable) {
                player->cur_pos = player->loop_offset;
            } else {
                player->playing = 0;
                break;
            }
        } else {
            /* Unsupported or unrecognized command; terminate stream safely. */
            player->playing = 0;
            break;
        }
    }

    return commands_executed;
}

/**
 * @brief Stops VGM soundtrack playback and resets remaining wait counters.
 *
 * @param player Pointer to VGM player state.
 */
void d32_vgm_player_stop(D32VgmPlayer *player)
{
    if (player) {
        player->playing = 0;
        player->wait_samples = 0;
    }
}
