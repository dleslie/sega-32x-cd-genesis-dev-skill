# Diabolical32X — Video Game Music (VGM) Composer Guide

This guide documents the modern composition pipeline for Diabolical32X on the Sega Genesis / 32X, enabling external composers to author music using standard modern trackers such as **Furnace Tracker**, **DefleMask**, or **BambooTracker** and export standard `.vgm` files directly into the game.

---

## 1. Sound Hardware Architecture

Music is driven entirely by the Genesis 68000 co-processor streaming standard VGM commands to the console sound chips without consuming SH-2 CPU cycles or SDRAM:
- **Yamaha YM2612 (OPN2)**: 6 FM synthesis channels (4 operators per channel), clocked at **7.670453 MHz** (NTSC).
- **Texas Instruments SN76489 (DCSG)**: 3 square-wave tone channels + 1 noise channel, clocked at **3.579545 MHz** (NTSC).
- **Master Audio Mixer**: Slave SH-2 mixes digital PCM sound effects (11,025 Hz stereo) into 32X PWM while the 68000 plays the VGM music stream.

---

## 2. Track Assignment & Engine Hooks

The engine uses the following track indices (signaled by the Master SH-2 via communication register `MARS_SYS_COMM6`):

| Index | Track Name | File Name in `assets/audio/vgm/` | Context |
|:---:|:---|:---|:---|
| **0** | **Cathedral** | `cathedral.vgm` | Tristram Cathedral (Levels 1–4) & Title Screen |
| **1** | **Tristram** | `tristram.vgm` | Town of Tristram Overworld Hub |
| **2** | **Catacombs** | `catacombs.vgm` | Ancient Catacombs (Levels 5–8) |
| **3** | **Caves** | `caves.vgm` | Magma Caverns (Levels 9–12) |
| **4** | **Hell** | `hell.vgm` | The Lair of Terror (Levels 13–16) |
| **5** | **Victory** | `victory.vgm` | Endgame Victory Screen & Credits Roll |
| **0xFF** | **Mute** | *(Built-in)* | Sound Options "Music OFF" or cutscenes |

---

## 3. Recommended Tracker Workflow: Furnace Tracker

[Furnace Tracker](https://github.com/tildearrow/furnace) is the recommended multi-system chiptune tracker for Diabolical32X.

### Setting Up a New Song
1. Open Furnace Tracker.
2. Select **Sega Genesis / Mega Drive** system configuration:
   - Sound Chip: `YM2612 + SN76489`
   - Clock Rate: `NTSC (7.670453 MHz / 3.579545 MHz)`
   - Timing: `60 Hz` (Timer A / VBlank)
3. Use the pre-configured project template: `tools/audio/diablo32x_template.fur`.

---

## 4. Authentic 4-Operator FM Instrument Presets

### Patch 1: 12-String Acoustic Steel Guitar (Matt Uelmen Lead)
- **Algorithm**: `4` (Op1→Op2→Out, Op3→Op4→Out)
- **Feedback**: `6` (applied to Op1)
- **Panning**: `Left (0x80)`
- **Operator Parameters**:
  - **Op 1 (Modulator 1)**: DT=+1, MUL=1, TL=22, AR=31, D1R=14, D2R=5, D1L=4, RR=10 (0x4A)
  - **Op 2 (Carrier 1)**: DT=0, MUL=3, TL=4, AR=31, D1R=8, D2R=4, D1L=3, RR=10 (0x3A)
  - **Op 3 (Modulator 2)**: DT=0, MUL=2, TL=28, AR=31, D1R=12, D2R=5, D1L=4, RR=10 (0x4A)
  - **Op 4 (Carrier 2)**: DT=0, MUL=1, TL=4, AR=31, D1R=7, D2R=3, D1L=3, RR=10 (0x3A)

### Patch 2: Acoustic Guitar Reverb / Echo (Streets of Rage 2 Style)
- Same parameters as Patch 1, but with **Panning: Right (0x40)** and Carrier Total Level attenuated to `TL=16` (0x49=16, 0x4D=16) for spatial depth and DAC headroom.

### Patch 3: Cathedral Choir Pad / Ethereal Swell
- **Algorithm**: `5` (Op1→Op2, Op3, Op4 all to Out)
- **Feedback**: `0`
- **Panning**: `Center (0xC0)`
- **Operator Parameters**:
  - **Op 1**: DT=+1, MUL=1, TL=28, AR=16, D1R=3, D2R=1, D1L=2, RR=8 (0x28)
  - **Op 2**: DT=-2, MUL=1, TL=18, AR=14, D1R=3, D2R=1, D1L=2, RR=8 (0x28)
  - **Op 3**: DT=0, MUL=2, TL=24, AR=16, D1R=3, D2R=1, D1L=2, RR=8 (0x28)
  - **Op 4**: DT=+1, MUL=1, TL=18, AR=14, D1R=3, D2R=1, D1L=2, RR=8 (0x28)

### Patch 4: Sub-Bass Pedal Drone
- **Algorithm**: `0` (Op1→Op2→Op3→Op4→Out)
- **Feedback**: `6`
- **Panning**: `Center (0xC0)`
- **Operator Parameters**:
  - **Op 1**: DT=0, MUL=1, TL=26, AR=31, D1R=3, D2R=0, D1L=2, RR=10 (0x2A)
  - **Op 2**: DT=0, MUL=1, TL=18, AR=31, D1R=3, D2R=0, D1L=2, RR=10 (0x2A)
  - **Op 3**: DT=0, MUL=1, TL=14, AR=31, D1R=3, D2R=0, D1L=2, RR=10 (0x2A)
  - **Op 4**: DT=0, MUL=1, TL=18, AR=31, D1R=3, D2R=0, D1L=2, RR=10 (0x2A)

---

## 5. Exporting and Building

1. In Furnace Tracker or DefleMask:
   - Set the loop point at the desired measure or pattern start.
   - Choose **File → Export → Video Game Music (*.vgm)**.
2. Save the exported `.vgm` file into `assets/audio/vgm/` matching the target track name:
   - e.g., `assets/audio/vgm/tristram.vgm`
3. Run:
   ```bash
   python3 tools/vgm_pack.py
   make
   ```
4. The build pipeline validates the VGM header, checks loop points, and packs the music stream directly into the 32X cartridge ROM.
