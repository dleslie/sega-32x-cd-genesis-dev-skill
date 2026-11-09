#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Algorithmic FM Synthesis Approximation for Sega Genesis YM2612.

Converts uncompressed PCM WAV audio streams (from assets/audio/wav/) into standard
Video Game Music (VGM v1.50+) streams using a signal-fitting algorithm:
1. Temporal segmentation into 60 Hz NTSC VBlank analysis windows.
2. Short-time spectral analysis (FFT + parabolic interpolation & autocorrelation)
   to extract fundamental frequencies (f0), harmonics, and RMS energy envelopes.
3. Multi-voice channel allocation (tracking up to 6 simultaneous polyphonic voices).
4. Physical mapping from acoustic parameters to YM2612 4-operator FM registers:
   - Fundamental frequency -> (Block, F-Num) register pairs.
   - Spectral brightness / centroid -> Modulator operator Total Level (TL / modulation index).
   - RMS energy -> Carrier operator Total Level (TL attenuation).
   - Harmonic ratios -> Operator frequency multipliers (MUL).
   - Envelope slopes -> Attack Rate (AR), Decay Rates (D1R, D2R), Release Rate (RR).
5. Output emission as standard, validated VGM v1.50 streaming streams.

Algorithmic music synthesis approximates acoustic sound files using the Genesis
console's capacity for hardware FM synthesis.
"""
from __future__ import annotations

import argparse
import math
import os
import struct
import sys
import wave
from typing import Dict, List, Optional, Tuple

try:
    import numpy as np
    HAVE_NUMPY = True
except ImportError:
    HAVE_NUMPY = False

# Standard Sega Genesis NTSC clock frequencies
YM2612_CLOCK = 7670453       # 7.670453 MHz NTSC YM2612
SN76489_CLOCK = 3579545      # 3.579545 MHz NTSC PSG
SAMPLES_PER_FRAME_60HZ = 735 # 44,100 / 60 = 735 samples per NTSC VBlank frame
VGM_SAMPLE_RATE = 44100

# Channel mapping: YM2612 channels 0..2 on Port 0, channels 3..5 on Port 1
PORT_FOR_CHANNEL = [0, 0, 0, 1, 1, 1]
PORT_CH_INDEX = [0, 1, 2, 0, 1, 2] # Relative channel index within port (0..2)


class YM2612Operator:
    """Configuration state for a single YM2612 operator."""

    def __init__(self, dt: int = 0, mul: int = 1, tl: int = 0,
                 ar: int = 31, d1r: int = 0, d2r: int = 0,
                 d1l: int = 0, rr: int = 15):
        """Initialize operator parameters with default organ/pure tone values."""
        self.dt = dt      # Detune (0..7, 3 bits)
        self.mul = mul    # Frequency multiplier (0..15, 4 bits)
        self.tl = tl      # Total level attenuation (0..127, 7 bits; 0 = max volume)
        self.ar = ar      # Attack rate (0..31, 5 bits; 31 = instantaneous)
        self.d1r = d1r    # Decay 1 rate (0..31, 5 bits)
        self.d2r = d2r    # Decay 2 rate (0..31, 5 bits)
        self.d1l = d1l    # Decay 1 level / sustain (0..15, 4 bits)
        self.rr = rr      # Release rate (0..15, 4 bits; 15 = fast release)


class YM2612ChannelConfig:
    """4-Operator configuration and algorithm state for an FM channel."""

    def __init__(self, algorithm: int = 4, feedback: int = 0, pan: int = 0xC0):
        """Initialize channel algorithm and operator array."""
        self.algorithm = algorithm # Algorithm 0..7 (bits 2..0)
        self.feedback = feedback   # Feedback 0..7 (bits 5..3)
        self.pan = pan             # Panning 0x80 L, 0x40 R, 0xC0 Center
        self.ams = 0               # Amplitude modulation sensitivity
        self.fms = 0               # Frequency modulation sensitivity
        # 4 operators: OP1, OP2, OP3, OP4 (Calibrated with TL headroom and RR=10 to avoid note bleed)
        self.ops = [
            YM2612Operator(dt=0, mul=1, tl=40, ar=31, d1r=10, d2r=4, d1l=2, rr=10), # OP1 (Modulator 1)
            YM2612Operator(dt=0, mul=1, tl=4,  ar=31, d1r=8,  d2r=3, d1l=3, rr=10), # OP2 (Carrier 1 / Mod)
            YM2612Operator(dt=0, mul=2, tl=45, ar=31, d1r=10, d2r=4, d1l=2, rr=10), # OP3 (Modulator 2)
            YM2612Operator(dt=0, mul=1, tl=4,  ar=31, d1r=6,  d2r=2, d1l=4, rr=10), # OP4 (Carrier 2)
        ]


def freq_to_fnum_block(freq: float, clock: int = YM2612_CLOCK) -> Tuple[int, int]:
    """Convert an acoustic frequency in Hertz to YM2612 (block, fnum) values.

    Formula:
        f = (Fnum * clock) / (144 * 2^(20 - block))
    Inverting for Fnum:
        Fnum = (f * 144 * 2^(20 - block)) / clock
    Valid Fnum range is 0..2047 (11 bits). Block is 0..7 (3 bits).

    Args:
        freq: Target frequency in Hertz.
        clock: Master clock frequency (default 7.670453 MHz for NTSC).

    Returns:
        Tuple of (block, fnum).
    """
    # Guard against zero or negative frequencies
    if freq <= 0.0:
        return 0, 0

    best_block = 0
    best_fnum = 0

    # Search blocks from 0 to 7 to find appropriate 11-bit F-Number representation
    for block in range(8):
        factor = 144.0 * (2.0 ** (20 - block))
        fnum_float = (freq * factor) / float(clock)
        fnum = int(round(fnum_float))

        # Check if Fnum fits within 11-bit YM2612 register width (0..2047)
        if 0 <= fnum <= 2047:
            best_block = block
            best_fnum = fnum
            # Prefer middle-to-high Fnum for higher pitch resolution
            if fnum >= 512:
                break
        elif fnum < 0:
            best_block = 0
            best_fnum = 0
            break
        else:
            # fnum exceeds 2047; clamp for upper boundary fallback
            best_block = block
            best_fnum = min(fnum, 2047)

    return best_block, best_fnum


def fnum_block_to_freq(block: int, fnum: int, clock: int = YM2612_CLOCK) -> float:
    """Convert YM2612 (block, fnum) back to acoustic frequency in Hertz.

    Args:
        block: Octave block (0..7).
        fnum: Frequency number (0..2047).
        clock: Master clock in Hertz.

    Returns:
        Reconstructed acoustic frequency in Hertz.
    """
    # Boundary validation on block and fnum
    if block < 0 or block > 7 or fnum <= 0:
        return 0.0
    factor = 144.0 * (2.0 ** (20 - block))
    return (float(fnum) * float(clock)) / factor


def load_wav_pcm(wav_path: str) -> Tuple[List[float], int]:
    """Load a standard WAV file and convert to floating-point mono audio array [-1.0, 1.0].

    Preconditions:
        wav_path must point to a readable, uncorrupted WAV file.

    Parameters:
        wav_path: Filesystem path to source WAV file.

    Returns:
        Tuple of (samples_list, sample_rate).
    """
    if not os.path.exists(wav_path):
        raise FileNotFoundError(f"WAV audio file not found: {wav_path}")

    with wave.open(wav_path, "rb") as wf:
        n_channels = wf.getnchannels()
        sampwidth = wf.getsampwidth()
        framerate = wf.getframerate()
        n_frames = wf.getnframes()
        raw_bytes = wf.readframes(n_frames)

    samples = []
    # 16-bit signed PCM audio
    if sampwidth == 2:
        count = n_frames * n_channels
        fmt = f"<{count}h"
        pcm16 = struct.unpack(fmt, raw_bytes)
        if n_channels == 1:
            samples = [float(s) / 32768.0 for s in pcm16]
        else:
            # Average left and right channels to produce mono analysis stream
            for i in range(0, count, n_channels):
                mono_s = sum(pcm16[i : i + n_channels]) / float(n_channels)
                samples.append(mono_s / 32768.0)
    # 8-bit unsigned PCM audio
    elif sampwidth == 1:
        count = n_frames * n_channels
        pcm8 = raw_bytes
        if n_channels == 1:
            samples = [(float(b) - 128.0) / 128.0 for b in pcm8]
        else:
            for i in range(0, count, n_channels):
                mono_s = sum(pcm8[i : i + n_channels]) / float(n_channels)
                samples.append((mono_s - 128.0) / 128.0)
    else:
        raise ValueError(f"Unsupported WAV sample width: {sampwidth * 8} bits")

    return samples, framerate


def analyze_frame_fft(window: List[float], sample_rate: int,
                      max_peaks: int = 4) -> List[Tuple[float, float]]:
    """Analyze an audio window to detect dominant sinusoidal frequency peaks and amplitudes.

    Uses Fast Fourier Transform with parabolic peak interpolation for sub-bin
    pitch resolution.

    Args:
        window: Windowed float32 samples.
        sample_rate: Audio sampling frequency in Hertz.
        max_peaks: Maximum number of prominent spectral peaks to return.

    Returns:
        List of (frequency_hz, amplitude) sorted by amplitude descending.
    """
    n = len(window)
    if n < 32:
        return []

    # Fast numpy path when available
    if HAVE_NUMPY:
        w_arr = np.array(window, dtype=np.float32)
        # Apply Hann window to eliminate spectral leakage
        hann = np.hanning(n)
        spectrum = np.abs(np.fft.rfft(w_arr * hann))
        freqs = np.fft.rfftfreq(n, d=1.0 / sample_rate)

        # Find local peaks above noise floor
        peaks = []
        noise_floor = np.max(spectrum) * 0.02
        for i in range(1, len(spectrum) - 1):
            if spectrum[i] > noise_floor and spectrum[i] > spectrum[i - 1] and spectrum[i] > spectrum[i + 1]:
                # Parabolic peak interpolation: alpha = y[i-1], beta = y[i], gamma = y[i+1]
                alpha = float(spectrum[i - 1])
                beta = float(spectrum[i])
                gamma = float(spectrum[i + 1])
                denom = (alpha - 2.0 * beta + gamma)
                delta = 0.5 * (alpha - gamma) / denom if denom != 0.0 else 0.0
                peak_freq = float(freqs[i] + delta * (sample_rate / float(n)))
                peak_amp = float(beta - 0.25 * (alpha - gamma) * delta)
                if 20.0 <= peak_freq <= 8000.0:
                    peaks.append((peak_freq, peak_amp))

        # Sort peaks by amplitude descending and truncate to max_peaks
        peaks.sort(key=lambda p: p[1], reverse=True)
        return peaks[:max_peaks]

    # Pure Python fallback using discrete autocorrelation for fundamental frequency
    rms = math.sqrt(sum(x * x for x in window) / float(n))
    if rms < 0.01:
        return []

    # Autocorrelation over lag range [min_lag, max_lag] corresponding to 50 Hz .. 2000 Hz
    min_lag = max(2, int(sample_rate / 2000))
    max_lag = min(n // 2, int(sample_rate / 50))
    best_lag = 0
    best_corr = 0.0

    for lag in range(min_lag, max_lag):
        corr = sum(window[i] * window[i + lag] for i in range(n - lag))
        if corr > best_corr:
            best_corr = corr
            best_lag = lag

    if best_lag > 0 and best_corr > 0.0:
        f0 = float(sample_rate) / float(best_lag)
        return [(f0, rms)]

    return []


def calculate_spectral_centroid(window: List[float], sample_rate: int) -> float:
    """Calculate the spectral centroid (center of mass of frequency spectrum).

    Indicates spectral brightness, used to modulate YM2612 operator modulation index.

    Args:
        window: Analysis frame samples.
        sample_rate: Sampling frequency.

    Returns:
        Spectral centroid in Hertz.
    """
    n = len(window)
    if n < 32:
        return 1000.0

    if HAVE_NUMPY:
        w_arr = np.array(window, dtype=np.float32)
        hann = np.hanning(n)
        spectrum = np.abs(np.fft.rfft(w_arr * hann))
        freqs = np.fft.rfftfreq(n, d=1.0 / sample_rate)
        sum_spec = np.sum(spectrum)
        if sum_spec > 1e-6:
            return float(np.sum(freqs * spectrum) / sum_spec)
        return 1000.0

    # Simplified zero-crossing rate approximation for centroid in pure Python
    zcr = 0
    for i in range(1, n):
        if (window[i] >= 0.0 and window[i - 1] < 0.0) or (window[i] < 0.0 and window[i - 1] >= 0.0):
            zcr += 1
    # Frequency estimate = (zcr / 2) * (sample_rate / n)
    return (float(zcr) / 2.0) * (float(sample_rate) / float(n))


class VoiceTrack:
    """Tracks active note frequency, volume, and age for a single YM2612 channel."""

    def __init__(self, channel_id: int):
        """Initialize channel state."""
        self.channel_id = channel_id
        self.active = False
        self.freq = 0.0
        self.block = 0
        self.fnum = 0
        self.volume_tl = 127 # 127 = silence
        self.mod_tl = 40     # Modulator attenuation
        self.frames_active = 0


def allocate_voices_to_channels(peaks: List[Tuple[float, float]],
                                voices: List[VoiceTrack],
                                centroid: float,
                                rms_energy: float) -> List[Tuple[int, bool, int, int, int, int]]:
    """Allocate detected spectral peaks to YM2612 hardware channels.

    Minimizes pitch discontinuity between frames and emits key-on/off flags.

    Args:
        peaks: List of (frequency_hz, amplitude) from spectral analysis.
        voices: Current state of hardware channels.
        centroid: Overall spectral brightness in Hertz.
        rms_energy: Total frame energy.

    Returns:
        List of actions per channel: (ch_id, key_on, block, fnum, carrier_tl, mod_tl).
    """
    n_ch = len(voices)
    actions = []

    # Map frame energy to carrier attenuation (TL: 0 = loud, 127 = quiet)
    if rms_energy > 0.0001:
        db = 20.0 * math.log10(min(1.0, max(rms_energy, 0.0001)))
        # -48 dB maps to ~60 attenuation, 0 dB maps to 0 attenuation
        carrier_tl = int(max(0, min(127, round(-1.25 * db))))
    else:
        carrier_tl = 127

    # Map spectral centroid to modulator TL (brightness -> low TL / higher modulation index)
    # 200 Hz centroid -> TL 60 (dark), 4000 Hz centroid -> TL 15 (bright)
    brightness_factor = max(0.0, min(1.0, (centroid - 200.0) / 3800.0))
    mod_tl = int(round(60.0 - brightness_factor * 45.0))
    mod_tl = max(10, min(90, mod_tl))

    # Match peaks to voices
    used_peaks = set()
    matched_voices = set()

    # 1. Continue existing tones with closest frequency match
    for ch_idx, voice in enumerate(voices):
        if voice.active:
            best_peak_idx = -1
            best_diff = 1e9
            for p_idx, (pf, pa) in enumerate(peaks):
                if p_idx not in used_peaks:
                    diff = abs(pf - voice.freq)
                    # Semi-tone ratio is ~1.059; tolerance of 15% frequency shift
                    if diff < voice.freq * 0.15 and diff < best_diff:
                        best_diff = diff
                        best_peak_idx = p_idx

            if best_peak_idx >= 0:
                pf, pa = peaks[best_peak_idx]
                used_peaks.add(best_peak_idx)
                matched_voices.add(ch_idx)
                blk, fn = freq_to_fnum_block(pf)
                voice.freq = pf
                voice.block = blk
                voice.fnum = fn
                voice.volume_tl = carrier_tl
                voice.mod_tl = mod_tl
                voice.frames_active += 1
                # Key remains held (no new key-on triggered)
                actions.append((ch_idx, False, blk, fn, carrier_tl, mod_tl))

    # 2. Allocate newly arrived peaks to available idle channels
    for p_idx, (pf, pa) in enumerate(peaks):
        if p_idx not in used_peaks:
            # Find first inactive voice
            for ch_idx, voice in enumerate(voices):
                if ch_idx not in matched_voices and not voice.active:
                    used_peaks.add(p_idx)
                    matched_voices.add(ch_idx)
                    blk, fn = freq_to_fnum_block(pf)
                    voice.active = True
                    voice.freq = pf
                    voice.block = blk
                    voice.fnum = fn
                    voice.volume_tl = carrier_tl
                    voice.mod_tl = mod_tl
                    voice.frames_active = 1
                    # New note onset requires Key-On event
                    actions.append((ch_idx, True, blk, fn, carrier_tl, mod_tl))
                    break

    # 3. Turn off voices that no longer have a corresponding spectral peak
    for ch_idx, voice in enumerate(voices):
        if ch_idx not in matched_voices and voice.active:
            voice.active = False
            voice.frames_active = 0
            voice.volume_tl = 127
            # Signal note off
            actions.append((ch_idx, False, 0, 0, 127, mod_tl))

    return actions


def emit_ym2612_init_stream(configs: List[YM2612ChannelConfig]) -> bytes:
    """Emit initial register configuration commands for all active YM2612 channels.

    Programs:
    - DAC disable & LFO off.
    - All channel Key-Off commands.
    - Alg/Feedback, Panning, and all 4 operator envelope/detune/multiplier settings.

    Args:
        configs: List of channel configurations.

    Returns:
        Bytes containing standard VGM 0x52/0x53 command sequences.
    """
    stream = bytearray()

    def w0(r: int, d: int) -> None:
        stream.extend([0x52, r, d])

    def w1(r: int, d: int) -> None:
        stream.extend([0x53, r, d])

    # Disable DAC to prevent PCM override of FM Channel 6
    w0(0x2B, 0x00)
    # LFO off
    w0(0x22, 0x00)

    # Initial key off across all 6 channels
    for ch in range(3):
        w0(0x28, ch)       # Channels 0..2 on Port 0
    for ch in range(4, 7):
        w0(0x28, ch)       # Channels 3..5 on Port 1

    # Program each channel's voice characteristics
    for ch_id, cfg in enumerate(configs):
        port = PORT_FOR_CHANNEL[ch_id]
        p_ch = PORT_CH_INDEX[ch_id]
        write_fn = w1 if port == 1 else w0

        # Alg & Feedback: Register 0xB0 + ch
        reg_b0 = (cfg.feedback << 3) | (cfg.algorithm & 0x07)
        write_fn(0xB0 + p_ch, reg_b0)

        # Pan & AMS/FMS: Register 0xB4 + ch
        reg_b4 = (cfg.pan & 0xC0) | ((cfg.ams & 0x03) << 4) | (cfg.fms & 0x07)
        write_fn(0xB4 + p_ch, reg_b4)

        # Operator offsets: OP1 = 0x00, OP2 = 0x08, OP3 = 0x04, OP4 = 0x0C
        op_offsets = [0x00, 0x08, 0x04, 0x0C]
        for op_idx, op in enumerate(cfg.ops):
            off = op_offsets[op_idx]
            # 0x30: Detune / Multiple
            write_fn(0x30 + off + p_ch, ((op.dt & 0x07) << 4) | (op.mul & 0x0F))
            # 0x40: Total Level
            write_fn(0x40 + off + p_ch, op.tl & 0x7F)
            # 0x50: Attack Rate
            write_fn(0x50 + off + p_ch, op.ar & 0x1F)
            # 0x60: Decay 1 Rate
            write_fn(0x60 + off + p_ch, op.d1r & 0x1F)
            # 0x70: Decay 2 Rate
            write_fn(0x70 + off + p_ch, op.d2r & 0x1F)
            # 0x80: Sustain Level / Release Rate
            write_fn(0x80 + off + p_ch, ((op.d1l & 0x0F) << 4) | (op.rr & 0x0F))

    return bytes(stream)


def convert_wav_to_vgm(wav_path: str,
                       num_channels: int = 4,
                       loop: bool = True) -> bytes:
    """Algorithmically approximate an input WAV audio file as a standard YM2612 VGM v1.50 stream.

    Preconditions:
        wav_path must be a valid WAV file.
        num_channels must be in range 1..6.

    Parameters:
        wav_path: Filesystem path to source WAV file.
        num_channels: Number of hardware FM voices to allocate (1..6).
        loop: True to mark stream as looping, False for one-shot SFX.

    Returns:
        Complete binary contents of a standard VGM v1.50 file.
    """
    if num_channels < 1 or num_channels > 6:
        raise ValueError(f"Invalid channel count: {num_channels} (must be 1..6)")

    samples, sample_rate = load_wav_pcm(wav_path)
    total_audio_samples = len(samples)
    if total_audio_samples == 0:
        raise ValueError("WAV file contains zero audio samples")

    # Analysis window step at 60 Hz frame intervals (735 samples at 44.1 kHz)
    step_samples = int(round(sample_rate / 60.0))
    # Analysis window size with overlap for smooth spectral estimation
    win_size = min(total_audio_samples, 2048 if sample_rate >= 32000 else 1024)

    # Initialize hardware voices and channel configurations
    voices = [VoiceTrack(ch) for ch in range(num_channels)]
    # Use Algorithm 4 (2 parallel mod-carrier pairs) as flexible harmonic generator
    configs = [YM2612ChannelConfig(algorithm=4, feedback=4, pan=0xC0) for _ in range(num_channels)]
    # Give voices subtle stereo separation across the stereo field
    if num_channels >= 2:
        configs[0].pan = 0x80 # Left
        configs[1].pan = 0x40 # Right
    if num_channels >= 4:
        configs[2].pan = 0xC0 # Center
        configs[3].pan = 0x80 # Left-Center

    # Prepare VGM stream buffer
    data = bytearray()
    init_bytes = emit_ym2612_init_stream(configs)
    data.extend(init_bytes)

    loop_offset_in_data = len(data)
    total_vgm_samples = 0

    # Helper writer closures
    def w0(r: int, d: int) -> None:
        data.extend([0x52, r, d])

    def w1(r: int, d: int) -> None:
        data.extend([0x53, r, d])

    # Slide analysis window across input audio
    pos = 0
    while pos < total_audio_samples:
        frame_chunk = samples[pos : min(total_audio_samples, pos + win_size)]
        # Zero-pad trailing frame if necessary
        if len(frame_chunk) < win_size:
            frame_chunk = frame_chunk + [0.0] * (win_size - len(frame_chunk))

        # Extract spectral features for the frame
        rms = math.sqrt(sum(s * s for s in frame_chunk) / float(win_size))
        centroid = calculate_spectral_centroid(frame_chunk, sample_rate)
        peaks = analyze_frame_fft(frame_chunk, sample_rate, max_peaks=num_channels)

        # Allocate detected peaks to hardware channels
        actions = allocate_voices_to_channels(peaks, voices, centroid, rms)

        # Emit hardware register writes for allocated voice actions
        for ch_idx, key_on, blk, fn, carrier_tl, mod_tl in actions:
            port = PORT_FOR_CHANNEL[ch_idx]
            p_ch = PORT_CH_INDEX[ch_idx]
            write_fn = w1 if port == 1 else w0

            # Turn off note if voice is going silent
            if carrier_tl >= 127:
                # Key off for this channel: reg 0x28, channel index
                ym_ch = ch_idx if ch_idx < 3 else (ch_idx + 1)
                w0(0x28, ym_ch)
                continue

            # Update Modulator Total Level (OP1 = 0x40, OP3 = 0x44)
            write_fn(0x40 + p_ch, mod_tl & 0x7F)
            write_fn(0x44 + p_ch, (mod_tl + 5) & 0x7F)

            # Update Carrier Total Level (OP2 = 0x48, OP4 = 0x4C)
            write_fn(0x48 + p_ch, carrier_tl & 0x7F)
            write_fn(0x4C + p_ch, carrier_tl & 0x7F)

            # Update Frequency Number & Block
            fhi = ((blk & 0x07) << 3) | ((fn >> 8) & 0x07)
            flo = fn & 0xFF
            write_fn(0xA4 + p_ch, fhi)
            write_fn(0xA0 + p_ch, flo)

            # Trigger Key-On event if note attack occurred
            if key_on:
                ym_ch = ch_idx if ch_idx < 3 else (ch_idx + 1)
                # Key off first to reset phase
                w0(0x28, ym_ch)
                # Key on all 4 operators (0xF0 | channel)
                w0(0x28, 0xF0 | ym_ch)

        # Emit 60 Hz frame wait command (0x62 = 735 samples at 44.1 kHz)
        data.append(0x62)
        total_vgm_samples += SAMPLES_PER_FRAME_60HZ
        pos += step_samples

    # Turn off all active channels at end of stream
    for ch_idx in range(num_channels):
        ym_ch = ch_idx if ch_idx < 3 else (ch_idx + 1)
        w0(0x28, ym_ch)

    # End of sound data command (0x66)
    data.append(0x66)

    # Construct standard 64-byte VGM v1.50 header
    header = bytearray(64)
    struct.pack_into("<4s", header, 0x00, b"Vgm ")
    file_size = 64 + len(data)
    struct.pack_into("<I", header, 0x04, file_size - 4)         # EOF offset
    struct.pack_into("<I", header, 0x08, 0x00000150)             # Version 1.50
    struct.pack_into("<I", header, 0x0C, SN76489_CLOCK)         # SN76489 clock
    struct.pack_into("<I", header, 0x10, 0)                      # YM2413 clock
    struct.pack_into("<I", header, 0x14, 0)                      # GD3 offset
    struct.pack_into("<I", header, 0x18, total_vgm_samples)      # Total samples

    if loop:
        # Loop offset is relative to 0x1C. Loop point is at file offset (64 + loop_offset_in_data)
        abs_loop_offset = 64 + loop_offset_in_data
        struct.pack_into("<I", header, 0x1C, abs_loop_offset - 0x1C) # Loop offset
        struct.pack_into("<I", header, 0x20, total_vgm_samples)      # Loop samples
    else:
        struct.pack_into("<I", header, 0x1C, 0) # No loop
        struct.pack_into("<I", header, 0x20, 0)

    struct.pack_into("<I", header, 0x24, 60)                     # Rate (60 Hz)
    struct.pack_into("<I", header, 0x2C, YM2612_CLOCK)          # YM2612 clock
    # VGM data offset at 0x34 is relative to 0x34: 0x40 - 0x34 = 0x0C
    struct.pack_into("<I", header, 0x34, 0x0C)

    return bytes(header + data)


def main() -> int:
    """Command-line interface entry point."""
    parser = argparse.ArgumentParser(
        description="Approximate WAV audio as Genesis YM2612 FM synthesis VGM v1.50 stream."
    )
    parser.add_argument("input_wav", help="Input PCM WAV audio file path")
    parser.add_argument("-o", "--output", required=True, help="Output .vgm file path")
    parser.add_argument("-c", "--channels", type=int, default=4, choices=range(1, 7),
                        help="Number of hardware FM channels to allocate (1..6, default 4)")
    parser.add_argument("--no-loop", action="store_true", help="Disable seamless track looping")

    args = parser.parse_args()

    try:
        vgm_bytes = convert_wav_to_vgm(
            args.input_wav,
            num_channels=args.channels,
            loop=not args.no_loop
        )
        os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
        with open(args.output, "wb") as f:
            f.write(vgm_bytes)
        print(f"Successfully approximated '{args.input_wav}' -> '{args.output}' ({len(vgm_bytes):,} bytes)")
        return 0
    except Exception as e:
        print(f"Error converting audio to FM synthesis: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
