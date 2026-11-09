/**
 * @file sub_cdda.h
 * @brief Sega CD Redbook CD-DA Audio Streaming & MSF Conversion Subsystem.
 *
 * Implements standard Redbook audio timing (75 Hz frames), 2-second lead-in pregap
 * (150 frames) compensation, LBA <-> MSF coordinate translation, track table (TOC)
 * navigation, and CD-DA playback state tracking on the Sub-CPU.
 *
 * Copyright (c) 2026 Dan Leslie
 * SPDX-License-Identifier: MIT
 */

#ifndef SUB_CDDA_H
#define SUB_CDDA_H

#include <stdint.h>
#include <stdbool.h>

#define CDDA_FRAMES_PER_SEC      75    /**< Standard Redbook audio frames (sectors) per second */
#define CDDA_SECS_PER_MIN        60    /**< Seconds per minute */
#define CDDA_FRAMES_PER_MIN      (CDDA_SECS_PER_MIN * CDDA_FRAMES_PER_SEC) /* 4500 */
#define CDDA_PREGAP_FRAMES       150   /**< Standard 2-second pregap offset (2 * 75 = 150) */
#define CDDA_MAX_TRACKS          99    /**< Maximum Redbook tracks per disc */

/* Playback State Enumeration */
typedef enum {
    CDDA_STATUS_STOPPED = 0,
    CDDA_STATUS_PLAYING = 1,
    CDDA_STATUS_PAUSED  = 2,
    CDDA_STATUS_SEEKING = 3
} CddaStatus;

/**
 * @struct CddaTimeMSF
 * @brief Time representation in Minutes, Seconds, and 75 Hz Frames.
 */
typedef struct {
    uint8_t minutes;
    uint8_t seconds;
    uint8_t frames;
} CddaTimeMSF;

/**
 * @struct CddaTrack
 * @brief Table of Contents (TOC) track descriptor.
 */
typedef struct {
    uint8_t  track_num;     /**< 1-based track identifier (1 = Data, 2..99 = Audio) */
    bool     is_audio;      /**< true if uncompressed 16-bit 44.1 kHz PCM audio */
    uint32_t start_lba;     /**< Starting sector LBA */
    uint32_t length_lba;    /**< Track length in sectors */
} CddaTrack;

/**
 * @brief Converts Minute-Second-Frame (MSF) coordinates to Logical Block Address (LBA).
 * Incorporates standard 150-frame (2-second) pregap offset.
 *
 * @param m Minutes (0..99).
 * @param s Seconds (0..59).
 * @param f Frames (0..74).
 * @return Computed LBA.
 */
uint32_t cdda_msf_to_lba(uint8_t m, uint8_t s, uint8_t f);

/**
 * @brief Converts Logical Block Address (LBA) to Minute-Second-Frame (MSF) coordinates.
 *
 * @param lba Sector LBA.
 * @param out_msf Output structure receiving decoded M, S, and F values.
 */
void cdda_lba_to_msf(uint32_t lba, CddaTimeMSF *out_msf);

/**
 * @brief Initializes the CD-DA audio subsystem and halts any active playback.
 */
void cdda_init(void);

/**
 * @brief Loads disc Table of Contents (TOC) track definitions.
 *
 * @param tracks Array of track descriptors.
 * @param count Number of tracks (up to CDDA_MAX_TRACKS).
 * @return true if valid track table was registered, false otherwise.
 */
bool cdda_set_toc(const CddaTrack *tracks, uint8_t count);

/**
 * @brief Initiates playback of a specific Redbook audio track.
 *
 * @param track_num 1-based track number (must be registered as audio track).
 * @return true if track was queued/started, false if track not found or is data.
 */
bool cdda_play_track(uint8_t track_num);

/**
 * @brief Initiates playback over an arbitrary sector range.
 *
 * @param start_lba Starting sector.
 * @param end_lba Terminating sector.
 * @return true if valid range and playback started, false otherwise.
 */
bool cdda_play_range(uint32_t start_lba, uint32_t end_lba);

/**
 * @brief Pauses current playback, preserving current head position.
 */
void cdda_pause(void);

/**
 * @brief Resumes paused playback from current head position.
 */
void cdda_resume(void);

/**
 * @brief Halts playback and resets drive state to idle.
 */
void cdda_stop(void);

/**
 * @brief Advances simulated drive playback by a given number of 75 Hz frames.
 *
 * @param elapsed_frames Number of frames elapsed since last update.
 */
void cdda_update(uint32_t elapsed_frames);

/**
 * @brief Queries current playback status.
 */
CddaStatus cdda_get_status(void);

/**
 * @brief Queries currently playing track number (0 if stopped).
 */
uint8_t cdda_get_current_track(void);

/**
 * @brief Queries current playback head LBA.
 */
uint32_t cdda_get_current_lba(void);

#endif /* SUB_CDDA_H */
