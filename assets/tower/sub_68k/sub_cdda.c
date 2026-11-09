/**
 * @file sub_cdda.c
 * @brief Sega CD Redbook CD-DA Audio Streaming & MSF Conversion Implementation.
 *
 * Implements MSF <-> LBA translations with 2-second pregap adjustment, Table of
 * Contents (TOC) registration, and drive playback state machine management.
 *
 * Copyright (c) 2026 Dan Leslie
 * SPDX-License-Identifier: MIT
 */

#include "sub_cdda.h"
#include <string.h>

static CddaTrack s_toc[CDDA_MAX_TRACKS];
static uint8_t s_toc_count = 0;

static CddaStatus s_status = CDDA_STATUS_STOPPED;
static uint8_t s_active_track = 0;
static uint32_t s_play_head_lba = 0;
static uint32_t s_play_end_lba = 0;

uint32_t cdda_msf_to_lba(uint8_t m, uint8_t s, uint8_t f)
{
    /* Calculate absolute frames from minute, second, and frame components */
    uint32_t total_frames = ((uint32_t)m * CDDA_FRAMES_PER_MIN) +
                            ((uint32_t)s * CDDA_FRAMES_PER_SEC) +
                            (uint32_t)f;

    /* Subtract standard 2-second lead-in pregap offset (150 frames) */
    if (total_frames < CDDA_PREGAP_FRAMES)
    {
        /* Coordinate falls within lead-in pregap area */
        return 0;
    }

    return total_frames - CDDA_PREGAP_FRAMES;
}

void cdda_lba_to_msf(uint32_t lba, CddaTimeMSF *out_msf)
{
    if (!out_msf)
    {
        /* Null destination pointer */
        return;
    }

    /* Add standard 2-second lead-in pregap offset (150 frames) to derive absolute disc time */
    uint32_t absolute_frames = lba + CDDA_PREGAP_FRAMES;

    out_msf->minutes = (uint8_t)(absolute_frames / CDDA_FRAMES_PER_MIN);
    uint32_t rem = absolute_frames % CDDA_FRAMES_PER_MIN;

    out_msf->seconds = (uint8_t)(rem / CDDA_FRAMES_PER_SEC);
    out_msf->frames  = (uint8_t)(rem % CDDA_FRAMES_PER_SEC);
}

void cdda_init(void)
{
    s_toc_count = 0;
    s_status = CDDA_STATUS_STOPPED;
    s_active_track = 0;
    s_play_head_lba = 0;
    s_play_end_lba = 0;
    memset(s_toc, 0, sizeof(s_toc));
}

bool cdda_set_toc(const CddaTrack *tracks, uint8_t count)
{
    if (!tracks || count == 0 || count > CDDA_MAX_TRACKS)
    {
        /* Invalid track array or count out of bounds */
        return false;
    }

    /* Store track definitions into internal Table of Contents */
    for (uint8_t i = 0; i < count; i++)
    {
        s_toc[i] = tracks[i];
    }
    s_toc_count = count;

    return true;
}

bool cdda_play_track(uint8_t track_num)
{
    if (s_toc_count == 0 || track_num == 0)
    {
        /* TOC uninitialized or invalid track number */
        return false;
    }

    /* Search TOC for matching track descriptor */
    for (uint8_t i = 0; i < s_toc_count; i++)
    {
        if (s_toc[i].track_num == track_num)
        {
            if (!s_toc[i].is_audio)
            {
                /* Cannot play data track as CD-DA audio */
                return false;
            }

            s_active_track = track_num;
            s_play_head_lba = s_toc[i].start_lba;
            s_play_end_lba = s_toc[i].start_lba + s_toc[i].length_lba;
            s_status = CDDA_STATUS_PLAYING;
            return true;
        }
    }

    /* Track number not found in registered TOC */
    return false;
}

bool cdda_play_range(uint32_t start_lba, uint32_t end_lba)
{
    if (end_lba <= start_lba)
    {
        /* Inverted or empty sector range specified */
        return false;
    }

    s_active_track = 0; /* Generic range playback */
    s_play_head_lba = start_lba;
    s_play_end_lba = end_lba;
    s_status = CDDA_STATUS_PLAYING;

    return true;
}

void cdda_pause(void)
{
    if (s_status == CDDA_STATUS_PLAYING)
    {
        /* Freeze optical pickup at current playback coordinate */
        s_status = CDDA_STATUS_PAUSED;
    }
}

void cdda_resume(void)
{
    if (s_status == CDDA_STATUS_PAUSED)
    {
        /* Unfreeze optical pickup and continue sector streaming */
        s_status = CDDA_STATUS_PLAYING;
    }
}

void cdda_stop(void)
{
    /* Halt playback mechanism and return to idle state */
    s_status = CDDA_STATUS_STOPPED;
    s_active_track = 0;
    s_play_head_lba = 0;
    s_play_end_lba = 0;
}

void cdda_update(uint32_t elapsed_frames)
{
    if (s_status != CDDA_STATUS_PLAYING)
    {
        /* Drive is not in active streaming state */
        return;
    }

    s_play_head_lba += elapsed_frames;

    /* Check if optical head reached designated track boundary */
    if (s_play_head_lba >= s_play_end_lba)
    {
        /* Automatic stop at end of playback range */
        cdda_stop();
    }
}

CddaStatus cdda_get_status(void)
{
    return s_status;
}

uint8_t cdda_get_current_track(void)
{
    return s_active_track;
}

uint32_t cdda_get_current_lba(void)
{
    return s_play_head_lba;
}
