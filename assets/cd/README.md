# Sega CD / Mega-CD Starter Project & Mastering Template

A complete, turnkey dual-processor development and disc mastering template for **Sega CD / Mega-CD**.

---

## Architecture Overview

The Sega CD pairs the Genesis Motorola 68000 (Main-CPU @ 7.67 MHz) with a dedicated secondary Motorola 68000 (Sub-CPU @ 12.5 MHz), 512 KB of Sub-CPU Program RAM, 256 KB of shared Word RAM, a hardware graphics ASIC, and an 8-channel Ricoh RF5C164 PCM audio chip.

```
┌────────────────────────────────────────────────────────┐
│               GENESIS MAIN 68000 (@ 7.67 MHz)          │
│   • Runs SGDK 2.x runtime                              │
│   • VDP Tilemaps, Sprites, Scrolling                   │
│   • Controller Polling & COMM Command Dispatch         │
└───────────────────────────┬────────────────────────────┘
                            │
               COMM Registers & Word RAM
                            │
┌───────────────────────────▼────────────────────────────┐
│               SEGA CD SUB-68000 (@ 12.5 MHz)           │
│   • 512 KB Program RAM (0x000000..0x07FFFF)            │
│   • 128 KB Word RAM 1M Ping-Pong Buffer (0x0C0000)     │
│   • Ricoh RF5C164 8-Channel PCM Audio (0xFF0000)       │
│   • CD-ROM Filesystem & Redbook CD-DA Streaming        │
└────────────────────────────────────────────────────────┘
```

---

## Directory Structure

```
assets/cd/
├── Makefile                # Convenience Makefile wrapper
├── README.md               # This documentation
└── src/
    ├── main_68k/
    │   ├── main_cd.c       # Genesis Main-CPU coordinator (SGDK 2.x)
    │   └── sub_payload.s   # Assembly container staging sub.bin into Main-CPU
    └── sub_68k/
        ├── sub_crt0.s      # Sub-CPU vector table & PRG-RAM startup
        ├── sub_asic.h      # Sega CD ASIC coprocessor registers & trace tables
        ├── sub_asic.c      # Affine rotation, zoom & stamp rendering engine
        ├── sub_pcm.h       # Ricoh RF5C164 PCM registers & Wave RAM banking
        ├── sub_pcm.c       # 8-channel audio driver, pitch & pan modulation
        ├── sub_bram.h      # Backup RAM (BRAM) 8 KB filesystem driver
        ├── sub_bram.c      # Format verification, save/load/delete records
        ├── sub_main.c      # Sub-CPU main loop & Word RAM renderer
        └── sub_68k.ld      # Sub-CPU 512 KB PRG-RAM linker script
```

---

## Building & Verification

The template can be compiled directly from `assets/cd/` or from the repository root:

```bash
# Build Sub-CPU binary, Main-CPU binary, and master game.chd / game.cue
make -C assets/cd

# Run host oracle tests and headless emulator boot verification
make -C assets/cd check

# Generate ISO and CUE sheet only (skip CHD compression)
make -C assets/cd cue

# Clean build artifacts
make -C assets/cd clean
```

Outputs are emitted to `out_cd/`:
- `out_cd/main.bin`: Genesis Main-CPU executable (with embedded `sub.bin` for turnkey PRG-RAM staging)
- `out_cd/sub.bin`: Sega CD Sub-CPU firmware
- `out_cd/game_track01.iso`: Mode 1 ISO-9660 data track
- `out_cd/game.cue`: Standard Redbook CUE sheet
- `out_cd/game.chd`: High-ratio compressed disc image (LZMA / Deflate / FLAC)

---

## Standalone Disc Mastering Script (`assets/build_disc.sh`)

Any directory of assets can be packaged into a bootable Sega CD disc image:

```bash
bash assets/build_disc.sh \
    -o out_cd/game.chd \
    -d build_cd/disc_staging \
    -a audio_tracks/ \
    -t "MY SEGA CD GAME" \
    -V "SEGA_CD_GAME"
```

### Options:
- `-o, --output <FILE>`: Destination disc image (`.chd`, `.cue`, or `.iso`)
- `-d, --data-dir <DIR>`: Directory of files to stage into the ISO-9660 filesystem
- `-b, --boot-header <FILE>`: Custom boot header / IP binary (sectors 0–15); auto-generated if omitted
- `-a, --audio-dir <DIR>`: Directory of 44.1 kHz 16-bit stereo `.wav` tracks for CD-DA audio
- `-V, --volume <LABEL>`: ISO-9660 volume identifier
- `-t, --title <TITLE>`: Disc header title (injected at `0x020` and `0x050`)
- `--cue-only`: Generate `.iso` and `.cue` without running `chdman`
