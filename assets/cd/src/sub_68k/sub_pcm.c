/**
 * @file sub_pcm.c
 * @brief Sega CD Ricoh RF5C164 8-Channel PCM Audio Processor Driver.
 *
 * Implements sample uploading to 64 KB Wave RAM via 4 KB banking, channel
 * envelope/pan configuration, frequency pitch modulation, and audio playback.
 *
 * Copyright (c) 2026 Dan Leslie
 * SPDX-License-Identifier: MIT
 */

#include "sub_pcm.h"
#include "sub_asic.h"

/* Channel Mute Bitmask (1 = Channel Muted, 0 = Channel Active) */
static uint8_t s_channel_mute_mask = 0xFF;

/**
 * @brief Selects an active channel (0..7) for subsequent register operations.
 */
void sub_pcm_select_channel(uint8_t channel)
{
    /* Bit 7 = 1 (Sound Enable), Bit 6 = 1 (Channel Select Mode), Bits 0-2 = Channel */
    SUB_PCM_REG_CTRL = (uint8_t)(PCM_CTRL_SOUND_ENABLE | PCM_CTRL_MODE_CHANNEL | (channel & 0x07));
}

/**
 * @brief Selects which 4 KB bank of the 64 KB Wave RAM is mapped into the Sub-CPU window.
 */
void sub_pcm_select_bank(uint8_t bank)
{
    /* Bit 7 = 1 (Sound Enable), Bit 6 = 0 (RAM Bank Select Mode), Bits 0-3 = Bank */
    SUB_PCM_REG_CTRL = (uint8_t)(PCM_CTRL_SOUND_ENABLE | PCM_CTRL_MODE_BANK | (bank & 0x0F));
}

/**
 * @brief Initializes the Ricoh RF5C164 sound processor and silences all channels.
 */
void sub_pcm_init(void)
{
    /* Master sound enable */
    SUB_PCM_REG_CTRL = PCM_CTRL_SOUND_ENABLE | PCM_CTRL_MODE_CHANNEL;

    /* Mute all 8 PCM audio channels */
    s_channel_mute_mask = 0xFF;
    SUB_PCM_REG_CHAN_ENABLE = s_channel_mute_mask;

    /* Initialize control registers across all 8 channels */
    for (uint8_t ch = 0; ch < PCM_TOTAL_CHANNELS; ch++)
    {
        sub_pcm_select_channel(ch);
        SUB_PCM_REG_ENV = 0x00;           /* Zero volume */
        SUB_PCM_REG_PAN = PCM_PAN_CENTER; /* Center stereo */
        SUB_PCM_REG_FDL = 0x00;
        SUB_PCM_REG_FDH = 0x00;
        SUB_PCM_REG_LSL = 0x00;
        SUB_PCM_REG_LSH = 0x00;
        SUB_PCM_REG_ST  = 0x00;
    }
}

/**
 * @brief Uploads 8-bit unsigned PCM sample data into Wave RAM.
 * Automatically sanitizes embedded 0xFF bytes to 0xFE and appends the 0xFF loop sentinel.
 */
void sub_pcm_load_sample(uint16_t wave_addr, const uint8_t *sample_data, uint16_t length)
{
    uint8_t current_bank = 0xFF;

    for (uint16_t i = 0; i < length; i++)
    {
        uint16_t target_addr = wave_addr + i;
        uint8_t bank = (uint8_t)(target_addr >> 12);
        uint16_t offset = target_addr & 0x0FFF;

        /* Switch 4 KB memory bank when crossing boundary */
        if (bank != current_bank)
        {
            sub_pcm_select_bank(bank);
            current_bank = bank;
        }

        /* Sanitize: RF5C164 treats 0xFF strictly as loop/stop sentinel */
        uint8_t byte_val = sample_data[i];
        if (byte_val == PCM_LOOP_STOP_SENTINEL)
        {
            byte_val = 0xFE;
        }

        /* Write to odd byte in Wave RAM aperture */
        SUB_PCM_WAVE_RAM_BASE[offset * 2] = byte_val;
    }

    /* Append hardware loop / stop sentinel at end of sample buffer */
    uint16_t sentinel_addr = wave_addr + length;
    uint8_t sentinel_bank = (uint8_t)(sentinel_addr >> 12);
    uint16_t sentinel_offset = sentinel_addr & 0x0FFF;

    if (sentinel_bank != current_bank)
    {
        sub_pcm_select_bank(sentinel_bank);
    }
    SUB_PCM_WAVE_RAM_BASE[sentinel_offset * 2] = PCM_LOOP_STOP_SENTINEL;
}

/**
 * @brief Initiates playback of a sample on the specified channel.
 */
void sub_pcm_play(uint8_t channel, uint16_t wave_addr, uint16_t loop_addr,
                  uint16_t freq_step, uint8_t volume, uint8_t pan)
{
    sub_pcm_select_channel(channel);

    SUB_PCM_REG_ENV = volume;
    SUB_PCM_REG_PAN = pan;
    SUB_PCM_REG_FDL = (uint8_t)(freq_step & 0xFF);
    SUB_PCM_REG_FDH = (uint8_t)((freq_step >> 8) & 0xFF);
    SUB_PCM_REG_LSL = (uint8_t)(loop_addr & 0xFF);
    SUB_PCM_REG_LSH = (uint8_t)((loop_addr >> 8) & 0xFF);
    SUB_PCM_REG_ST  = (uint8_t)((wave_addr >> 8) & 0xFF);

    /* Unmute channel in master enable register (0 = Active) */
    s_channel_mute_mask &= (uint8_t)~(1 << (channel & 0x07));
    SUB_PCM_REG_CHAN_ENABLE = s_channel_mute_mask;
}

/**
 * @brief Stops and mutes audio output on the specified channel.
 */
void sub_pcm_stop(uint8_t channel)
{
    /* Mute channel in master enable register (1 = Muted) */
    s_channel_mute_mask |= (uint8_t)(1 << (channel & 0x07));
    SUB_PCM_REG_CHAN_ENABLE = s_channel_mute_mask;
}

/**
 * @brief Dynamically updates the playback pitch / frequency of an active channel.
 */
void sub_pcm_set_pitch(uint8_t channel, uint16_t freq_step)
{
    sub_pcm_select_channel(channel);
    SUB_PCM_REG_FDL = (uint8_t)(freq_step & 0xFF);
    SUB_PCM_REG_FDH = (uint8_t)((freq_step >> 8) & 0xFF);
}

/**
 * @brief Dynamically updates stereo panning on an active channel.
 */
void sub_pcm_set_pan(uint8_t channel, uint8_t pan)
{
    sub_pcm_select_channel(channel);
    SUB_PCM_REG_PAN = pan;
}

/**
 * @brief Synthesizes and stages a clean multi-harmonic synth wave into Wave RAM.
 */
void sub_pcm_stage_synth_tone(void)
{
    /* Select Bank 0 (0x0000..0x0FFF in Wave RAM) */
    sub_pcm_select_bank(0);

    /* Synthesize 512-sample dual-harmonic musical tone */
    for (uint16_t i = 0; i < 512; i++)
    {
        /* Primary harmonic (1 cycle across 512 samples) */
        uint8_t angle1 = (uint8_t)(i >> 1);
        int16_t s1 = sub_sin(angle1);

        /* Second harmonic (2 cycles across 512 samples) */
        uint8_t angle2 = (uint8_t)(i & 0xFF);
        int16_t s2 = sub_sin(angle2);

        /* Blend harmonics centered at 128 (8-bit unsigned PCM): 128 + 80*s1/2048 + 30*s2/2048 */
        int32_t val = 128 + (((int32_t)s1 * 80) >> 11) + (((int32_t)s2 * 30) >> 11);

        if (val > 254)
        {
            val = 254;
        }
        else if (val < 1)
        {
            val = 1;
        }

        /* Write to odd byte */
        SUB_PCM_WAVE_RAM_BASE[i * 2] = (uint8_t)val;
    }

    /* Place loop sentinel at offset 512 */
    SUB_PCM_WAVE_RAM_BASE[512 * 2] = PCM_LOOP_STOP_SENTINEL;
}
