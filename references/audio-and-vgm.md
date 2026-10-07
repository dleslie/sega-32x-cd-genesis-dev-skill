# Audio Architecture, VGM Engine, SGDK XGM2 Tooling & Sound Drivers

This reference provides a comprehensive guide to audio systems across the Sega Genesis, Sega CD, and Sega 32X, detailing the **Video Game Music (VGM v1.50+)** specification, SGDK's **XGM2** sound driver and toolchain, tracker workflows, 32X PWM software mixers, and the MIDI-to-VGM translation pipeline.

---

## 1. Audio Hardware Overview Across Platforms

| Subsystem | Chips / Hardware | Capabilities | Primary Role |
| :--- | :--- | :--- | :--- |
| **Genesis FM/PSG** | Yamaha YM2612 + TI SN76489 | 6 FM channels (4-op) + 3 PSG square + 1 PSG noise. Channel 6 doubles as an 8-bit DAC PCM channel. | Chiptune BGM, rhythmic percussion, classic FM synth effects. |
| **32X PWM** | Mars PWM Stereo Audio Unit | Stereo 12-bit PWM FIFOs, programmable cycle rate (~11.025–22.05 kHz). | High-quality PCM voice lines, streamed software tracker beds, SFX mixing. |
| **Sega CD PCM** | Ricoh RF5C164 | 8-channel 8-bit PCM @ up to 32 kHz, 16-level stereo panning per channel, 64 KB Wave RAM. | Multi-voice polyphonic digital sound effects and orchestral sample playback. |
| **Sega CD CD-DA** | CD-ROM Drive Redbook Audio | 16-bit 44.1 kHz uncompressed stereo streaming directly from CD-ROM tracks. | Full studio-recorded soundtrack music without CPU overhead. |

---

## 2. SGDK Sound Architecture: XGM2 vs XGM

Genesis sound hardware (YM2612 + SN76489) is driven by an onboard **Z80 processor** executing dedicated sound driver firmware out of 8 KiB Z80 RAM. The Motorola 68000 kicks off playback commands and feeds data buffers; the Z80 executes the per-tick timing and register writes.

### A. Sound Drivers
- **XGM2** (Current SGDK Default):
  - Tooling: `xgm2tool`.
  - Up to 4 simultaneous PCM channels mixed at custom rates.
  - Resource type in `.res`: `XGM2`.
  - Recommended for all modern Genesis development.
- **XGM** (Legacy SGDK Driver):
  - Tooling: `xgmtool`.
  - Resource type in `.res`: `XGM`.
  - Found in legacy tutorials and older projects.

Both drivers compile standard **VGM** logs (register dumps) into packed binary formats tailored for efficient Z80 parsing.

### B. Music Declaration & Playback in SGDK
Declare the track in your `res/resources.res` file:
```res
XGM2 bgm_level1 "music/level1.vgm"
```
SGDK's `rescomp` invokes `xgm2tool` automatically to pack the VGM into a compiled C array.

Control playback in C:
```c
#include <genesis.h>
#include "resources.h"

// Play background music (resource symbol is a const u8[])
XGM2_play(bgm_level1);

// Multi-track bank variant
XGM2_playTrack(trackId);

// Playback transport controls
XGM2_pause();
XGM2_resume();
XGM2_stop();

// Looping (-1 = infinite loop, 0 = play once, N = loop N times)
XGM2_setLoopNumber(-1);

if (XGM2_isPlaying()) {
    // Music is actively playing
}
```

### C. Sound Effects: WAV to PCM via XGM2
Declare PCM sound effects in `res/resources.res` specifying driver and playback rate (in Hz):
```res
WAV sfx_jump "sfx/jump.wav" XGM2 11025     # <driver> <rate_hz>; mono, 8-bit unsigned
```

Play samples in C across 4 virtual PCM channels (`SOUND_PCM_CH1..SOUND_PCM_CH4`):
```c
// Basic trigger on channel 1
XGM2_playPCM(sfx_jump, sizeof(sfx_jump), SOUND_PCM_CH1);

// Extended playback: specify channel, priority (0-15), half-rate flag, and looping flag
XGM2_playPCMEx(sfx_jump, sizeof(sfx_jump), SOUND_PCM_CH2, 10, FALSE, FALSE);

// Stop channel
XGM2_stopPCM(SOUND_PCM_CH1);
```
*Rule of thumb:* Keep SFX short and sampled at modest rates (~8–13 kHz) to conserve cartridge ROM and bus bandwidth.

### D. The Bus Starvation Gotcha (Why Sound Stutters)
The Z80 sound driver requires access to the cartridge bus to fetch music and PCM data every tick. If the 68000 executes **massive DMA transfers during active display** (e.g. streaming full-plane tilemaps or road textures), the 68000 bus arbiter locks out the Z80.
- **Symptom:** Audio dropouts, stuttering, and tempo hitches that fire in sync with heavy visual events.
- **Mitigation:** Bound DMA transfers to VBlank (~18 KB NTSC), split big memory copies across frames, and never hold the Z80 bus request line asserted longer than necessary.

### E. Alternative Genesis Drivers
If SGDK's drivers do not match project requirements:
- **MDSDRV**: Flexible driver with FM/PSG pitch effects and separate SFX priorities.
- **Echo**: Lightweight sound driver by Oerg866 with zero bus-locking and dynamic sound priority queues.
- **MiniMusic**: Ultra-minimal music player for ROM-constrained projects.

---

## 3. Standard Video Game Music (VGM v1.50+) Specification

The **VGM specification** (https://vgmrips.net/wiki/VGM_Specification) is the standard format for logging and playing cycle-accurate chip audio.

### A. VGM Header Structure (Little-Endian)
A valid VGM file begins with a 64-byte (`0x40`) header:

| Offset | Size | Type | Field Description | Value for Sega Genesis |
| :--- | :--- | :--- | :--- | :--- |
| `0x00` | 4 | ASCII | Magic identification string | `"Vgm "` (`0x56 0x67 0x6D 0x20`) |
| `0x04` | 4 | uint32 | End of file relative offset (`file_size - 4`) | File size minus 4 |
| `0x08` | 4 | uint32 | VGM version number (BCD) | `0x00000150` for version 1.50 |
| `0x0C` | 4 | uint32 | SN76489 (PSG) clock frequency in Hz | `3579545` (NTSC) / `3546895` (PAL) |
| `0x10` | 4 | uint32 | YM2413 clock frequency | `0` (Unused) |
| `0x14` | 4 | uint32 | GD3 tag relative offset (metadata title/author) | Offset or `0` if omitted |
| `0x18` | 4 | uint32 | Total number of 44.1 kHz audio samples in track | Track duration in samples |
| `0x1C` | 4 | uint32 | Loop point relative offset from `0x1C` | Relative offset to loop start command, or `0` if non-looping |
| `0x20` | 4 | uint32 | Number of 44.1 kHz samples in loop portion | Loop sample count |
| `0x24` | 4 | uint32 | Recording frame rate | `60` (NTSC) or `50` (PAL) |
| `0x2C` | 4 | uint32 | Yamaha YM2612 clock frequency in Hz | `7670453` (NTSC) / `7600489` (PAL) |
| `0x34` | 4 | uint32 | Relative offset to start of VGM data stream from `0x34` | Usually `0x0C` (data starts at `0x40`) |

### B. Core Command Byte Stream
The command stream encodes register writes and sample waits:
- **`0x50 dd`**: Write data byte `dd` to the SN76489 PSG.
- **`0x52 aa dd`**: Write data byte `dd` to YM2612 Port 0 (Register `aa`, Channels 1–3).
- **`0x53 aa dd`**: Write data byte `dd` to YM2612 Port 1 (Register `aa`, Channels 4–6).
- **`0x61 nn nn`**: Wait `nn nn` samples (16-bit unsigned little-endian word; sample rate = 44,100 Hz).
- **`0x62`**: Wait 735 samples (exactly 1/60th of a second for 60 Hz NTSC frames).
- **`0x63`**: Wait 882 samples (exactly 1/50th of a second for 50 Hz PAL frames).
- **`0x70`..`0x7F`**: Wait `(cmd & 0x0F) + 1` samples (wait 1 to 16 samples).
- **`0x66`**: End of sound data. If the track is set to loop, playback rewinds to `0x1C + loop_offset`.

---

## 4. Composition Workflow: Furnace Tracker & DefleMask

Using standard tracker tools allows musicians and composers to work with familiar interfaces and immediately test output on hardware.

### A. Authoring in Furnace Tracker
1. Download **Furnace Tracker** (open-source, cross-platform: https://github.com/tildearrow/furnace).
2. Create a new song with system preset: **Sega Genesis / Mega Drive** (or chip list: *Yamaha YM2612 + Texas Instruments SN76489*).
3. Set the project clock rate:
   - Clock: NTSC (YM2612 = 7,670,453 Hz, SN76489 = 3,579,545 Hz).
   - Time base: 60 Hz tick or 44.1 kHz sample rate.
4. Channel Configuration:
   - Channels 1–5: 4-operator FM melodic leads, chords, basslines, and pads.
   - Channel 6: Melodic FM or dedicated 8-bit DAC drum samples.
   - Channels 7–9: PSG square wave leads, harmonies, or arpeggios.
   - Channel 10: PSG noise percussion (hats, snares, white noise bursts).
5. Setting Loop Points:
   - In Furnace, use order-list loop markers (`Bxx` position jump) to set seamless loop points.
6. Export:
   - Select **File → Export → VGM...**.
   - Furnace writes a fully standard VGM v1.50+ stream with headers and loop metadata intact.

### B. DefleMask Workflow
- Set target system to **Genesis / Mega Drive**.
- Save module as `.dmf`.
- Select **Export to VGM**. DefleMask generates clean `.vgm` streams ready for the game's asset pipeline.

---

## 5. Porting Music Pipelines (S3M/MOD/XM → VGM → XGM2)

Source games often ship tracker modules (`.s3m`, `.mod`, `.xm`) or AdLib/OPL sequences. A proven pipeline (worked example: `convert-s3m-vgm-jazzjackrabbit-md`):
1. **Identify Source Format:** S3M/MOD patterns, MIDI sequences, or AdLib FM register logs.
2. **Convert to VGM:** Re-orchestrate or route the module through an automated converter targeting the YM2612 + SN76489. Hand-tune FM instrument patches to approximate original OPL/tracker timbres.
3. **Compile to XGM2:** Declare the `.vgm` in `res/resources.res` using the `XGM2` directive (`rescomp` invokes `xgm2tool`).
4. **Audit and Verify:** Test playback in BlastEm or capture headless PCM to verify loop points, timing, and volume balance.

---

## 6. Sega 32X PWM Audio Engine & Software Mixer

The 32X produces sound through a **stereo PWM** unit that reconstructs analog audio from digital FIFO writes:
- Registers:
  - `0x20004030`: PWM Control (mode and clock divider).
  - `0x20004032`: PWM Cycle (sets sample rate: `Cycle = PWM_Clock / Rate`).
  - `0x20004034`: Left Channel FIFO.
  - `0x20004036`: Right Channel FIFO.
  - `0x20004038`: Mono FIFO (writes identical amplitude to both L and R).

### A. 8-Voice Software Mixer Design
Because the 32X has no dedicated sound co-processor, a software mixer runs on one of the SH-2 CPUs:
- Allocate $N$ virtual voices (typically 8), each with:
  - Sample pointer and length.
  - Fixed-point fractional position increment (pitch).
  - Loop start and loop end markers.
  - Volume and stereo panning scalars.
- **Per Output Sample:** Sum active voice outputs (`sample[pos] * vol`), apply stereo pan, clamp to 12-bit PWM range (2..1032), and push to the FIFOs.
- **Voice Stealing for SFX:** Reserve voice 8 for high-priority SFX by stealing an active music lane rather than adding a ninth channel. The next music note reclaims the channel.

### B. Avoiding FIFO Starvation (The "Buzz" Artifact)
The hardware PWM FIFO holds only a small number of samples (4–8 words). Sustained full-screen framebuffer writes from the SH-2 can lock the bus and prevent FIFO refills, causing an audible 60 Hz buzz.
- **Remedies:**
  1. Dedicate the **Slave SH-2** exclusively to audio mixing.
  2. Double-buffer or dirty-rectangle render on the Master SH-2 so drawing operations are bounded.
  3. Pre-initialize both framebuffers before launching PWM.

### C. Streaming BGM as IMA-ADPCM on the Slave SH-2
Full soundtracks cannot fit in ROM as raw PCM. Store music as **4-bit IMA-ADPCM** (~4:1 compression ratio):
- Decode in real time on the Slave SH-2 into 16-bit PCM at ~11.025 or 22.050 kHz.
- Stream data directly from cartridge ROM (uncached), requiring zero SDRAM for the music buffer.
- Coordinate with the Master SH-2 over **COMM registers**:
  - `COMM2`: BGM command word (`seq | track_id`). Edge-detected by sequence number so repeating the same track triggers re-play.
  - `COMM4`: SFX trigger command.
  - `COMM6`: Slave heartbeat counter (proves audio core has not crashed).

---

## 7. MIDI-to-VGM Pipeline (`midi2vgm`)

When porting games with MIDI soundtracks to Genesis FM/PSG hardware, use a structured 4-stage pipeline:
1. **Ingest:** Build an absolute tempo map, parse pitch bend/sustain (CC64), map volume curves, and isolate GM channel 10 for drums.
2. **Intermediate Representation (IR):** Chip-agnostic event model (`Note`, `DrumHit`, `Part`, `Curve`).
3. **Map & Allocate:**
   - **Channel Budget:** 6 FM + 3 PSG tone + 1 PSG noise + 1 DAC.
   - **Route by musical part, not by individual note:** Keep timbral consistency across phrases.
   - Parts PSG would ruin claim FM first (PSG cannot sound below ~C2 without pitch clamping, and loses pitch resolution in high registers).
   - Thin dense chords to root + top note *only* when pools are oversubscribed.
4. **Emit:**
   - Emit time-sorted register writes and sample waits into a VGM v1.61+ stream.
   - **The Retrigger Gap Trap:** Insert a **1.5 ms gap** between key-off and key-on. Emitting them at the exact same timestamp prevents the envelope generator from seeing the release, silencing notes.
   - **The 0x2B DAC Init Trap:** Overwriting register `0x2B = 0x00` after DAC startup silences sampled drums. Reserve FM Channel 6 strictly for DAC percussion.
   - Mix by musical role (lead −6 dB, pad +12 dB) to prevent accompaniment pads from drowning melodies.
   - Verify generated VGM against cycle-accurate emulators (Nuked-OPN2) before embedding in ROM.

---

## 8. Sega CD Audio: CD-DA and Ricoh RF5C164 PCM

- **CD-DA Redbook Audio:**
  - 16-bit 44.1 kHz stereo audio streaming directly from CD-ROM sectors.
  - Triggered via Sub-CPU BIOS calls (`_CDBPLAY`, `_CDBSTOP`, `_CDBPAUSE`).
  - Zero CPU overhead, zero RAM footprint. Pre-seek tracks during screen transitions to mask drive seek latency (~800 ms).
- **Ricoh RF5C164 8-Channel PCM:**
  - 8 independent 8-bit PCM channels with 16-level stereo panning per channel.
  - Dedicated 64 KiB Wave RAM accessible by Sub-CPU.
  - Up to 32 kHz sample rate per channel. Ideal for dynamic polyphonic sound effects, voices, and interactive sound beds.
