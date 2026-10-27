/**
 * @file sub_pcm.h
 * @brief Sega CD Ricoh RF5C164 8-Channel PCM Audio Processor Driver.
 *
 * Provides register definitions, Wave RAM banking utilities, channel configuration,
 * and real-time frequency/panning modulation routines for the Sega CD PCM sound chip.
 *
 * Copyright (c) 2026 Dan Leslie
 * SPDX-License-Identifier: MIT
 */

#ifndef SUB_PCM_H
#define SUB_PCM_H

#include <stdint.h>
#include <stdbool.h>

/* --- Ricoh RF5C164 Hardware Register Offsets (Odd Bytes on Sub-68K Bus) --- */
#define SUB_PCM_REG_ENV          (*(volatile uint8_t *)0xFF0001)  /**< Volume / Envelope (0..255)            */
#define SUB_PCM_REG_PAN          (*(volatile uint8_t *)0xFF0003)  /**< Stereo Pan (Bits 4-7: L, Bits 0-3: R) */
#define SUB_PCM_REG_FDL          (*(volatile uint8_t *)0xFF0005)  /**< Frequency Step Low Byte               */
#define SUB_PCM_REG_FDH          (*(volatile uint8_t *)0xFF0007)  /**< Frequency Step High Byte              */
#define SUB_PCM_REG_LSL          (*(volatile uint8_t *)0xFF0009)  /**< Loop Address Low Byte                 */
#define SUB_PCM_REG_LSH          (*(volatile uint8_t *)0xFF000B)  /**< Loop Address High Byte                */
#define SUB_PCM_REG_ST           (*(volatile uint8_t *)0xFF000D)  /**< Sample Start Address (High Byte / 256)*/
#define SUB_PCM_REG_CTRL         (*(volatile uint8_t *)0xFF000F)  /**< Master Control & Channel/Bank Select  */
#define SUB_PCM_REG_CHAN_ENABLE  (*(volatile uint8_t *)0xFF0011)  /**< Channel Mute Bitmask (1=Mute, 0=Play) */

/* 4 KB Bank Window to 64 KB Wave RAM (Odd Bytes: 0xFF2001..0xFF3FFF) */
#define SUB_PCM_WAVE_RAM_BASE    ((volatile uint8_t *)0xFF2001)

/* Control Register Bitfields */
#define PCM_CTRL_SOUND_ENABLE    0x80  /**< Master Sound Enable (Bit 7)       */
#define PCM_CTRL_MODE_CHANNEL    0x40  /**< Mode 1: Lower bits select Channel */
#define PCM_CTRL_MODE_BANK       0x00  /**< Mode 0: Lower bits select RAM Bank*/

/* Panning Helpers */
#define PCM_PAN_CENTER           0xFF  /**< Full stereo (L: 15, R: 15)        */
#define PCM_PAN_LEFT             0xF0  /**< Full left (L: 15, R: 0)           */
#define PCM_PAN_RIGHT            0x0F  /**< Full right (L: 0, R: 15)          */

/* Playback Constants */
#define PCM_PITCH_NORMAL         0x0800 /**< 1:1 playback rate (2048 in 5.11 format) */
#define PCM_PITCH_OCTAVE_UP      0x1000 /**< +1 Octave (4096 in 5.11 format)         */
#define PCM_PITCH_OCTAVE_DOWN    0x0400 /**< -1 Octave (1024 in 5.11 format)         */

#define PCM_TOTAL_CHANNELS       8
#define PCM_BANK_SIZE_BYTES      4096
#define PCM_LOOP_STOP_SENTINEL   0xFF   /**< Hardware end-of-sample / loop sentinel  */

/**
 * @brief Initializes the Ricoh RF5C164 sound processor and silences all channels.
 */
void sub_pcm_init(void);

/**
 * @brief Selects an active channel (0..7) for subsequent register operations.
 *
 * @param channel Channel index (0..7).
 */
void sub_pcm_select_channel(uint8_t channel);

/**
 * @brief Selects which 4 KB bank of the 64 KB Wave RAM is mapped into the Sub-CPU window.
 *
 * @param bank Bank index (0..15).
 */
void sub_pcm_select_bank(uint8_t bank);

/**
 * @brief Uploads 8-bit unsigned PCM sample data into Wave RAM.
 * Automatically sanitizes embedded 0xFF bytes to 0xFE and appends the 0xFF loop sentinel.
 *
 * @param wave_addr Target byte address in 64 KB Wave RAM (0..65535).
 * @param sample_data Pointer to source 8-bit unsigned PCM buffer.
 * @param length Sample length in bytes.
 */
void sub_pcm_load_sample(uint16_t wave_addr, const uint8_t *sample_data, uint16_t length);

/**
 * @brief Initiates playback of a sample on the specified channel.
 *
 * @param channel Target channel index (0..7).
 * @param wave_addr Start address in Wave RAM (must be 256-byte page aligned).
 * @param loop_addr Loop address in Wave RAM.
 * @param freq_step Frequency step in 5.11 fixed-point (0x0800 = 1.0x).
 * @param volume Master volume envelope (0..255).
 * @param pan Stereo panning byte (Bits 4-7: Left 0..15, Bits 0-3: Right 0..15).
 */
void sub_pcm_play(uint8_t channel, uint16_t wave_addr, uint16_t loop_addr,
                  uint16_t freq_step, uint8_t volume, uint8_t pan);

/**
 * @brief Stops and mutes audio output on the specified channel.
 *
 * @param channel Target channel index (0..7).
 */
void sub_pcm_stop(uint8_t channel);

/**
 * @brief Dynamically updates the playback pitch / frequency of an active channel.
 *
 * @param channel Target channel index (0..7).
 * @param freq_step Frequency step in 5.11 fixed-point.
 */
void sub_pcm_set_pitch(uint8_t channel, uint16_t freq_step);

/**
 * @brief Dynamically updates stereo panning on an active channel.
 *
 * @param channel Target channel index (0..7).
 * @param pan Panning byte (Bits 4-7: Left 0..15, Bits 0-3: Right 0..15).
 */
void sub_pcm_set_pan(uint8_t channel, uint8_t pan);

/**
 * @brief Synthesizes and stages a clean multi-harmonic synth wave into Wave RAM.
 */
void sub_pcm_stage_synth_tone(void);

#endif /* SUB_PCM_H */
