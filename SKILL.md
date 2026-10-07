---
name: sega-32x-CD-genesis-gamedev
description: >-
  Create, port, debug, and optimize games for the Sega Genesis / Mega Drive,
  Sega CD / Mega-CD, and Sega 32X (Sega Mars) ecosystems, including 32X CD.
  Combines SGDK (Motorola 68000, VDP tilemaps/sprites, Z80/YM2612/PSG, XGM/XGM2,
  rescomp), Mega-CD (Sub-CPU 68000, Word RAM 1M/2M modes, ASIC scaling/rotation,
  Ricoh RF5C164 PCM, CD-DA), and 32XDK / d32xr (dual Hitachi SH-2, direct-color
  VDP framebuffers, PWM stereo audio, Sega SSF 32 MiB banked ROM mapper, 68000 Mars
  co-processor handshakes). Features the standard Video Game Music (VGM v1.50+)
  engine and composition pipeline (Furnace Tracker, DefleMask), the decoupled
  two-stage intermediate asset pipeline (uncompressed PNG/WAV intermediate formats
  with post-clone extraction), software-3D/voxel/2D engines, and automated
  two-tier testing (host C logic oracle + headless PicoDrive / Genesis Plus GX
  verification). Trigger on any 16-bit or 32-bit Sega console development request.
---

# Sega Genesis, Sega CD, and 32X Game Development & Porting

This skill turns any game idea, port target, or existing Sega homebrew project into
a **verified, playable ROM or disc image** for:
- **Sega Genesis / Mega Drive** (`.bin` cartridge, C via SGDK 2.x)
- **Sega CD / Mega-CD** (`.iso` / `.chd` / `.bin`+`.cue`, Sub-CPU 68000, ASIC graphics, CD-DA/PCM)
- **Sega 32X / Sega Mars** (`.32x` cartridge, dual SH-2 + 68000, 32XDK)
- **Sega 32X CD** (The unified 32X + CD + Genesis hardware stack)
- **Sega "Tower of Power"** (The complete 4-tier stack: Genesis + 32X + Sega CD + Mega EverDrive PRO / X7)

It encodes the complete hardware models, toolchains, build systems, optimization
playbooks, audio engines (VGM v1.50+, FM/PSG, PWM, PCM, CD-DA), decoupled intermediate
asset pipelines, and two-tier automated testing (host logic oracles + headless emulators)
that prevent the ubiquitous failure mode: **a ROM that compiles cleanly but boots to a black screen**.

---

## The Target Hardware Spectrum

| Platform | Primary CPU(s) | Co-Processor(s) | Video / Graphics | Audio Hardware | Memory & Storage |
|:---|:---|:---|:---|:---|:---|
| **Sega Genesis** | Motorola 68000 @ 7.67 MHz | Z80 @ 3.58 MHz | VDP: 2 scroll planes (A/B), 1 window, 80 sprites, 4×16 palettes (9-bit BGR) | Yamaha YM2612 (6-ch FM) + TI SN76489 (PSG: 3 square + 1 noise) | 64 KB Work RAM, 64 KB VRAM, 8 KB Z80 RAM, cartridges up to 4 MiB (or banked) |
| **Sega CD** | Genesis 68000 + Sub-CPU 68000 @ 12.5 MHz | ASIC graphics processor, CD-ROM controller | Genesis VDP + ASIC hardware scaling, rotation, stamping into Word RAM | CD-DA Redbook audio + Ricoh RF5C164 (8-ch 8-bit PCM with stereo panning) | +512 KB Sub-CPU PRG RAM, +256 KB Word RAM (1M/2M ping-pong), 64 KB PCM wave RAM, 540 MiB CD-ROM |
| **Sega 32X** | Dual Hitachi SH-2 @ 23 MHz (Master & Slave) | Genesis 68000 @ 7.67 MHz (I/O, timers, sound) | 32X VDP: 2 framebuffers (double-buffered), 320×224 / 256×224, 8bpp indexed (256/256 CRAM) or 15bpp direct | 32X Stereo PWM FIFO (~11–22 kHz 12-bit) + Genesis YM2612 + PSG | 256 KB SDRAM (shared SH-2), 2×128 KB Framebuffer VRAM, cartridges up to 4 MiB base or 32 MiB (SSF mapper) |
| **Sega 32X CD** | Dual SH-2 + Genesis 68000 + Sub-CPU 68000 | 32X VDP + Genesis VDP + Mega-CD ASIC | 32X framebuffer overlaid on Genesis planes with ASIC-rendered textures | PWM + YM2612 + PSG + CD-DA + Ricoh RF5C164 PCM | Full combination: 256 KB SDRAM + 512 KB CD PRG + 256 KB Word RAM + 64 KB 68K RAM + CD-ROM storage |
| **Tower of Power** | Dual SH-2 + Main-68K + Sub-68K + Z80 | Mega EverDrive PRO/X7 (EDIO) + ASIC | 32X direct color VDP + Genesis planes + ASIC Word RAM | 4-Way Audio: YM2612 + PSG + Ricoh PCM + CD-DA + 32X PWM | Complete Stack: SDRAM + PRG RAM + Word RAM + WRAM + MicroSD FAT streaming + USB `edlink` |

---

## Sega CD / Mega-CD Hardware & Architecture (Deep-Dive)

The Sega CD is a formidable computing and multimedia expansion that turns the Genesis
into a dual-68000 system with hardware-accelerated affine graphics and CD audio.

### 1. Dual-CPU Architecture: Main-CPU vs Sub-CPU
- **Main-CPU (Genesis 68000 @ 7.67 MHz)**:
  - Manages Genesis VDP (tilemaps, sprites, scrolling), joypad controller input, and communications with the Sega CD.
  - Accesses the Sega CD BIOS ROM (`0x000000..0x01FFFF`) at boot.
  - Controls Word RAM access assignment and communicates via the Communication Registers at `0xA12000..0xA1202F`.
- **Sub-CPU (Mega-CD 68000 @ 12.5 MHz)**:
  - Runs at **1.6× clock speed** compared to the Main-CPU.
  - Controls the CD-ROM drive mechanism, sector data reading, CD-DA Redbook audio playback, and the Ricoh PCM sound chip.
  - Owns the **ASIC graphics processor** and can run full 3D math, physics, or scene decomposition in parallel with the Main-CPU.
  - Executes from 512 KB of high-speed Program RAM (`PRG-RAM`).

### 2. Word RAM: 1M Mode Ping-Pong vs 2M Mode
Word RAM is the 256 KB shared canvas between the Sub-CPU/ASIC and the Genesis VDP.
Controlled by bit 2 of the Memory Control Register (`0xA12003` Main / `0xFF8003` Sub):
- **1M Mode (`MODE = 1`) — Ping-Pong Double Buffering**:
  - Word RAM is split into two **128 KB banks** (Bank 0 and Bank 1).
  - One bank is mapped to the Sub-CPU (`0x0C0000..0x0DFFFF`) while the other bank is mapped to the Main-CPU (`0x200000..0x21FFFF`).
  - **The 60 FPS Graphics Loop**:
    - Sub-CPU & ASIC render the next frame (polygons, Mode 7 scaled/rotated road/sprites) into Bank 0.
    - Main-CPU transfers previously rendered Bank 1 via DMA into Genesis VRAM for display.
    - At VBlank, both CPUs toggle the `RET` / `DMNA` handshake bits to swap banks instantly without stalling.
- **2M Mode (`MODE = 0`) — Single Unified 256 KB Bank**:
  - The entire 256 KB block is mapped continuously (`0x200000..0x23FFFF` on Main / `0x080000..0x0BFFFF` on Sub).
  - Best for streaming Full-Motion Video (FMV), loading massive scene datasets, or transferring large uncompressed graphics.

### 3. ASIC Hardware Graphics Co-Processor
The Sega CD contains dedicated hardware for affine transformations, rotation, scaling, and stamping:
- **Operations**:
  - Real-time arbitrary rotation (0° to 360°).
  - Continuous scaling/zooming from 1/256 to 256× magnification.
  - High-speed tile stamping: Reads source tile patterns from PRG-RAM/Word RAM and renders transformed scanlines directly into Word RAM.
- **ASIC Parameter Table**:
  - Configured by writing a 16-word transformation parameter block (center $X/Y$, screen dimensions, and delta step vectors $dx, dxy, dy, dyx$).
  - Once triggered, the ASIC executes autonomously, freeing the Sub-CPU to process game logic, audio, or CD-ROM transfers.

### 4. Audio Systems: CD-DA Redbook Audio & Ricoh RF5C164 PCM
- **CD-DA Redbook Audio**:
  - Direct 16-bit 44.1 kHz uncompressed stereo audio streaming directly from CD-ROM disc tracks into console audio outputs.
  - Controlled by simple Sub-CPU BIOS calls (`_CDBPLAY`, `_CDBSTOP`, `_CDBPAUSE`).
  - **Zero CPU overhead and zero RAM consumption**: Gives CD games CD-quality orchestral or studio music for free.
- **Ricoh RF5C164 PCM Sound Chip**:
  - 8 independent PCM voice channels.
  - 8-bit unsigned PCM playback at sample rates up to 32 kHz.
  - 64 KB dedicated PCM Wave RAM for sample data.
  - Each channel features independent 16-level stereo panning ($L/R$) and automatic loop flags.
  - Ideal for multi-voice sound effects, ambient environmental audio, and digital voice acting concurrent with CD-DA music.

### 5. CD-ROM File System & Streaming Architecture
- **Drive Characteristics**: Single-speed (1×) CD-ROM drive (sustained transfer rate: **150 KB/s**, average seek latency: ~400 ms).
- **Filesystem**: Standard **ISO-9660 Mode 1** filesystem (2048 bytes per data sector).
- **Streaming Pipeline**:
  - Stream data sectors asynchronously into Sub-CPU Program RAM buffers while playing background CD-DA music.
  - Group track assets, voice clips, and level maps sequentially on the disc physical layout to eliminate seek latency.
- **BIOS Services**:
  - `_CDBBOOT`: Initializes drive and verifies disc presence.
  - `_CDBSTAT`: Queries drive state (seeking, reading, audio playing, tray open).
  - `_CDBREAD`: Asynchronously reads $N$ sectors into PRG-RAM.
  - `_CDBPLAY`: Plays Redbook CD audio from starting logical sector number to ending sector.
- **Save Games (BRAM)**:
  - 16 KB internal non-volatile Backup RAM (`BRAM`) plus optional Backup RAM Cartridges (128 KB+).
  - Managed via BIOS file services (`_BRMINIT`, `_BRMREAD`, `_BRMWRITE`).

### 6. Disc Image Mastering Pipeline
- **Format**: `.cue` file paired with `track01.iso` (ISO-9660 data track) and `.wav` tracks (CD-DA audio tracks).
- **Compression**: For testing and distribution, convert to compressed **CHD (Compressed Hunks of Data)**:
  ```bash
  chdman createcd -i game.cue -o game.chd
  ```
- Headless emulators (PicoDrive, Genesis Plus GX) boot `.chd` or `.cue` images directly.

---

## Learnings from Landmark Sega Codebases & Disassemblies

Study the techniques and patterns developed in these reference projects:

### 1. `sega-vr-disasm` (Virtua Racing 32X Static Recompilation)
- **Repository**: https://github.com/matiaszanolli/sega-vr-disasm
- **CPU Workload Split**:
  - 68000: Game logic, scene management, car physics, track progression, and SH-2 command list generation.
  - Master SH-2: High-speed command dispatch, coordinate transforms, and block copies.
  - Slave SH-2: Dedicated 3D polygon pipeline (transforms, clipping, and flat-shaded triangle rasterization).
- **The Bottleneck & 60 FPS Breakthrough**:
  - The original 32X game ran at ~20 FPS due to **blocking synchronization** (the 68000 waited synchronously for the Slave SH-2 to finish rendering, and waited synchronously for VBlank).
  - **Optimization to 40–60 FPS**:
    1. **Swap-Only V-INT Handler**: Make the V-INT handler do nothing except trigger the hardware framebuffer flip (`MARS_VDP_FS ^= 1`).
    2. **Decoupled Physics & Camera Interpolation**: Run physics simulation at a fixed tick rate (e.g. 30 Hz or 20 Hz) while rendering at 60 FPS by interpolating camera and object transforms between physics states.
    3. **Double-Buffered Command Lists**: Double-buffer command lists in SDRAM so the SH-2 renders frame $N$ while the 68000 computes frame $N+1$.

### 2. `s1disasm` (Sonic the Hedgehog Canonical Genesis Architecture)
- **Repository**: https://github.com/sonicretro/s1disasm
- **Dynamic Pattern Load Cues (PLC)**:
  - Solves the VDP DMA bandwidth limit (~7.6 KB per active frame) by queuing tile uploads across successive VBlank periods instead of blasting all tiles in one frame.
- **Dual-Sensor Terrain Collision**:
  - Terrain is structured into 16×16 tile blocks grouped into 256×256 chunks.
  - Height arrays (16 bytes per tile) and angle collision arrays allow high-speed slope physics and loop-de-loops using simple 68000 sensor raycasts without complex polygon math.
- **Object Manager Loop**:
  - Dynamic actor array using fixed-size object execution slots in 68000 Work RAM (`0xFFFFD000`).
  - Strict priority order, parent/child entity linkages, and camera-distance culling (`DeleteObject` when off-screen).
- **Parallax Plane Scrolling**:
  - Dynamic per-line and per-cell scroll offsets configured in VDP VSRAM and H-scroll tables during HBlank raster interrupts.

### 3. `d32xr` (DOOM 32X: Resurrection Architecture Notes)
- **Repository**: https://github.com/viciious/d32xr *(Note: Portions derived from Jaguar DOOM subject to id Software's source license are explicitly excluded and removed from this skill; only high-level architectural techniques are referenced)*
- **Multi-Phase Dual-SH2 Rendering**:
  - Divides rendering into explicit pipelined phases:
    - Master SH-2: BSP tree traversal, node clipping, wall segment generation (`r_phase1.c`..`r_phase4.c`).
    - Slave SH-2: Wall column rasterization and floor/ceiling span rendering (`r_phase5.c`..`r_phase8.c`).
- **Assembly Column Drawers (`sh2_draw.s`)**:
  - Highly optimized Hitachi SH-2 assembly using dual-issue pipeline scheduling, register caching, and precomputed reciprocal division lookup tables.
- **Cache Purging & Cache-Through Addressing**:
  - SH-2 4 KB cache is direct-mapped. DMA buffers and inter-CPU command blocks use the **cache-through address aperture** (`0x20000000` bit 29) to ensure CPUs never read stale cache lines.
  - Explicit cache purging is executed after copying textures into SDRAM.
- **PWM Audio Mixing**:
  - Slave SH-2 decodes 4-bit IMA-ADPCM straight from ROM, feeding stereo PWM FIFOs at 22.05 kHz without using SDRAM and without FIFO starvation.

### 4. `marsdev` (Modern Unified Build System)
- **Repository**: https://github.com/andwn/marsdev
- **Multi-Target Compilation**:
  - Clean directory separation for `m68k-elf` (Genesis Main-CPU & CD Sub-CPU) and `sh-elf` (32X Master/Slave).
  - Unifies library linking (newlib, libgcc) and linker script memory configurations.
- **Automated Disc Mastering**:
  - Integrated targets to build Sega CD disc images (`genisoimage` for ISO-9660, bundling CD-DA audio tracks, and compressing with `chdman`).

### 5. `32XDK` (Chilly Willy's 32X Devkit)
- **Repository**: https://github.com/viciious/32XDK
- **Hardware Initialization Sequence**:
  - Genesis 68000 boots first, writes `ADEN = 1` to `0xA15100` to unlock 32X hardware, sets up VDP lines, releases SH-2 reset (`nRES = 1` in `0xA15102`), and hands execution to SH-2 `mars_start.s`.
- **COMM Register Protocol**:
  - Establishes the 8 COMM registers as standard bidirectional synchronization primitives between Genesis 68000 and SH-2s.

---

## Core Pillars & Key Innovations

### 1. Decoupled Two-Stage Intermediate Asset Pipeline
Raw source assets (proprietary archives, PAKs, WADs, MPQs, DOS formats) must **never**
be directly converted into target binary blobs in a single monolithic compile step.
Instead, use a decoupled two-stage pipeline:

```
[Raw Sources / Archive]
       │
       ▼  Stage 1: make extract-assets (One-time post-clone repo setup)
[Intermediate Assets: assets/]
  ├── Uncompressed PNGs (spritesheets, textures, UI) + JSON metadata manifests
  ├── Uncompressed 16-bit 44.1 kHz WAVs (sound effects, voice lines, audio cues)
  └── Standard VGM v1.50 files (music composed in Furnace Tracker / DefleMask)
       │
       ▼  Stage 2: make (Normal build process)
[Target Baked Binaries]
  ├── Genesis: SGDK rescomp (tilesets, sprite definitions, maps, XGM2)
  ├── 32X: Packed palette bitmaps, linear screen blobs, bank-packed SSF pages
  └── Sega CD: Word RAM stamp maps, Ricoh PCM blocks, ISO-9660 disc filesystem
```

- **Why**: Allows artists and modders to inspect, edit, and replace assets in standard
  editors (Aseprite, Photoshop, Audacity, Furnace) without touching C code or proprietary extractors.
- **Portability**: Standard `make` runs purely from `assets/` without requiring external proprietary data files.
- See `references/asset-pipeline.md` for full implementation patterns.

### 2. Standard Video Game Music (VGM v1.50+) Audio Engine
Rather than relying on closed sound drivers or raw software synthesizers, audio composition
leverages the open **VGM (Video Game Music) v1.50+** specification:
- **Composition**: Author tracks in modern cross-platform tracker tools like **Furnace Tracker**
  or **DefleMask**, targeting the Genesis soundchips (YM2612 FM + SN76489 PSG).
- **Format**: Standard `.vgm` stream capturing register writes with 44.1 kHz sample-accurate wait commands:
  - `0x52 aa dd`: YM2612 port 0 register write
  - `0x53 aa dd`: YM2612 port 1 register write
  - `0x50 dd`: SN76489 PSG write
  - `0x61 nn nn`: Wait *n* 44.1 kHz samples
  - `0x62` / `0x63`: Wait 735 (60 Hz NTSC) / 882 (50 Hz PAL) samples
  - `0x70..0x7F`: Wait 1..16 samples
  - `0x66`: End of sound data / seamless loop rewind
- **Runtime Player**: Interrupt-driven 68000 or Z80 stream parser running on the Genesis side,
  decoupling music completely from 32X SH-2 or Mega-CD Sub-CPU compute workloads.
- See `references/audio-and-vgm.md` for specification details, parser code, and tracker workflows.

### 3. Extended Cartridge Architecture: Sega SSF Banked Mapper (Up to 32 MiB)
When game assets exceed the standard 4 MiB Genesis/32X cartridge limit:
- Use the **Sega SSF Mapper** (hardware standard from *Super Street Fighter II*):
  - Control registers: `0xA130F1..0xA130FF` (slots 0..7).
  - Page granularity: 512 KiB per page, addressing up to 64 pages (32 MiB total).
  - Slots 0..5 (`0x02000000..0x022FFFFF`): Fixed base 3 MiB ROM (code, engine, initial assets).
  - Slots 6 & 7 (`0x02300000` & `0x02380000`): Dynamic 512 KiB switchable apertures for data streaming.
  - PicoDrive integration: Autodetects SSF via ROM size > 4 MiB or `"SEGA SSF"` header string.
- See `references/architecture.md` and `assets/bank_packer.py`.

### 4. Motorola 68000 Critical Assembly Constraints
- **Word Branch Range (`.w`)**: In Motorola 68000 assembly, short branches (`bra.s`, `beq.s`, `bne.s`)
  are restricted to a signed 8-bit offset (-128 to +127 bytes).
- When writing 68000 resident handlers, interrupt routines, or loop bodies spanning larger blocks,
  always use **word branches (`.w`)** or unconditional jumps (`jmp`) to prevent assembler relocation failures.

### 5. Mandatory Fixed-Point Math & libfixmath (Zero-FPU Hardware)
- **Zero-FPU Architecture**: Neither Motorola 68000, Z80, nor Hitachi SH-2 possess a hardware Floating-Point Unit (FPU).
  Emulating IEEE 754 floats in software costs hundreds to thousands of clock cycles per operation (`__mulsf3`, `__divsf3`).
- **Speedup**:
  - 68000: Q16.16 addition is 8 cycles (`add.l`); multiplication is ~100 cycles (5× to 20× faster than software float).
  - SH-2: Q16.16 multiplication executes in **4 cycles** using hardware integer MAC instructions (`dmuls.l` + `xtrct`), ~25× faster than float.
- **Minimal libfixmath Distribution**: Bundled in `assets/fixmath/` providing Q16.16 arithmetic, saturating operations, pure 32-bit integer square root, and zero-RAM polynomial trigonometry (`sin`, `cos`, `atan2`).
- See `references/fixed-point-math.md` and `assets/fixmath/`.

---

## Definition of Done (Do Not Stop Early)

Never stop at "it compiled." A compiling black screen is the default failure mode of retro console ports.
Verify every milestone against this checklist:

1. **It compiles** — `make` produces a `.32x`, `out/rom.bin`, or `.iso` with zero errors.
2. **It links within RAM limits**:
   - Genesis: Work RAM (68 KB) not blown; `.data + .bss` fits.
   - 32X: SDRAM (256 KB) not blown; `__bss_end` is safely below SH-2 stacks (`< 0x0603F800`).
   - Sega CD: Sub-CPU PRG RAM (512 KB) and Word RAM allocations strictly bounded.
3. **It is not a black screen** — A headless emulator (PicoDrive or Genesis Plus GX) boots the real binary
   and asserts that rendered frames are lit, colourful, and changing over time.
4. **It is playable** — Scripted controller inputs advance through real game states (boot → title → menu → gameplay).
5. **The output binary is accessible** — Copied to the designated output directory (`rom/`, `out/`, or `release/`).

## Toolchain Provisioning & Extraction Rules

Toolchains, SDKs, and compiler binaries MUST ALWAYS be extracted or installed into **`$(PROJECT_ROOT)/opt`** and NEVER into the host system `/opt` or user home folders:
- **Prebuilt Git-LFS Toolchains (Instant & Recommended)**: The repository ships complete, hermetic GCC 14.2.0 cross-compilers (`m68k-elf` and `sh-elf`), SGDK 2.x, and tools in `artifacts/` tracked by Git LFS. Running `bash assets/setup.sh` unpacks them into `$(PROJECT_ROOT)/opt` in ~3 seconds with zero build dependencies.
- **Docker Support**: Build cleanly in isolation using the bundled `Dockerfile` and `assets/docker/run.sh` (`./assets/docker/run.sh make -f assets/Makefile.tower`).
- **Project Isolation**: Keeping toolchains in `$(PROJECT_ROOT)/opt/` ensures the project is completely self-contained, reproducible, and does not require `sudo` or root privileges.
- **Standard Locations**:
  - SGDK: `$(PROJECT_ROOT)/opt/m68k-elf` (set `GDK ?= $(PROJECT_ROOT)/opt/m68k-elf`).
  - MarsDev: `$(PROJECT_ROOT)/opt` (set `MARSDEV ?= $(PROJECT_ROOT)/opt`).
  - 32X Toolchains: `$(PROJECT_ROOT)/opt/sh-elf` (set `PATH := $(PROJECT_ROOT)/opt/sh-elf/bin:$(PATH)`).
  - Tool Symlinks: `$(PROJECT_ROOT)/opt/bin` (`PATH := $(PROJECT_ROOT)/opt/bin:$(PATH)`).
- **Forbidden**: Never extract tarballs or git clones to `/opt/`. In sandboxed environments and CI runners, modifying system `/opt` will fail with permission errors.

---

## Canonical Project Layouts

### A. Sega 32X Project Layout (Chilly Willy 32XDK)
```
game-32x/
├── Makefile                    # GENDEV ?= $(PROJECT_ROOT)/opt/toolchains/sega
├── opt/                        # Project toolchains: opt/toolchains/sega or opt/marsdev
├── src/
│   ├── core/                   # Portable C11: game logic, physics, entity models
│   │                           #   (Zero hardware calls, compiled for host tests too)
│   └── platform/
│       ├── 32x/                # SH-2 shell: main, hw/VDP, palette, audio, input
│       │   ├── mars.ld         # SH-2 linker script (assets/mars.ld)
│       │   ├── mars_start.s    # SH-2 startup / ROM header + embedded 68000 binary
│       │   ├── mars_bank.c     # SSF banking API (Mars_SetBankPage)
│       │   └── md_src/         # 68000 resident: VBlank, controller, VGM sound driver
│       └── sdl/                # Desktop reference shell (test oracle)
├── assets/                     # Intermediate uncompressed assets
│   ├── sprites/                # Uncompressed PNG sprite sheets + JSON metadata
│   ├── tilesets/               # Uncompressed PNG tilesets
│   ├── audio/
│   │   ├── vgm/                # Standard VGM v1.50 music tracks
│   │   └── wav/                # Standard 16-bit 44.1 kHz PCM sound effects
│   └── ui/                     # UI textures and layouts
├── tools/                      # Asset converters, romfix, bank_packer, vgm_pack
├── tests/                      # Host unit tests + PicoDrive headless harness
│   ├── host/                   # Pure C test suites run via host compiler
│   ├── harness.c               # Headless libretro emulator harness
│   └── run_tests.py            # Automated runner + pixel/frame assertions
└── rom/                        # Final verified .32x cartridge ROM
```

### B. Sega Genesis Project Layout (SGDK 2.x)
```
game-genesis/
├── Makefile                    # GDK ?= $(PROJECT_ROOT)/opt/sgdk  +  include $(GDK)/makefile.gen
├── opt/                        # Project toolchains: opt/sgdk, opt/bin
├── src/
│   ├── main.c                  # int main(bool hardReset) — entry + game loop
│   ├── rom_header.c            # const ROMHeader rom_header = {...} (region, SRAM)
│   ├── core/                   # Portable C game logic (host-tested)
│   └── platform/               # SGDK shell: VDP, SPR, PAL, JOY, XGM2 glue
├── inc/                        # Project headers
├── res/                        # resources.res descriptors (IMAGE, SPRITE, XGM2, WAV)
├── assets/                     # Uncompressed PNG/WAV intermediate source assets
├── out/                        # Build output: out/rom.bin, .lst, .map
└── tests/                      # Host oracle + Genesis Plus GX headless tests
```

### C. Sega CD / Mega-CD Project Layout
```
game-segacd/
├── Makefile                    # Multi-target build compiling Main-CPU, Sub-CPU, and ISO
├── opt/                        # Project toolchains: opt/bin, opt/sgdk, opt/marsdev
├── src/
│   ├── main_68k/               # Main-CPU 68000: VDP display, joypad, Word RAM DMA
│   ├── sub_68k/                # Sub-CPU 68000: CD-ROM streaming, ASIC 3D/Mode 7, Ricoh PCM
│   └── core/                   # Portable C core logic (host tested)
├── assets/                     # Uncompressed PNG textures/stamps, 16-bit WAVs, CD-DA tracks
├── disc/                       # ISO-9660 filesystem staging directory
│   ├── track01.iso             # Mode 1 data track
│   └── track02.wav...          # Redbook CD-DA audio tracks
└── out/                        # Final disc image (game.chd or game.cue + bin)
```

### D. Sega Tower of Power Project Layout (Unified 4-Tier Stack)
```
game-tower/
├── Makefile                    # Multi-architecture build (assets/Makefile.tower)
├── Dockerfile                  # Containerized build environment
├── artifacts/                  # Prebuilt GCC 14.2.0 toolchains in Git-LFS
├── opt/                        # Local unpacked toolchains: opt/m68k-elf, opt/sh-elf
├── src/
│   ├── boot/                   # Tower detection & handshakes (assets/tower/boot/)
│   │   ├── tower.h             # Hardware flags, registers, handshake prototypes
│   │   └── tower_boot.c        # Hardware detection, 32X/CD init, Word RAM swap
│   ├── main_md.c               # Genesis Main-CPU master coordinator (SGDK runtime)
│   ├── sh2/                    # 32X Dual SH-2 codebase (assets/tower/sh2/)
│   │   ├── mars_crt0.s         # Master & Slave vector init, stack, cache, handshake
│   │   ├── mars_master.c       # Master SH-2 VDP direct-color rendering
│   │   └── mars_slave.c        # Slave SH-2 math coprocessor & 12-bit PWM audio
│   ├── sub_68k/                # Sega CD Sub-CPU codebase (assets/tower/sub_68k/)
│   │   ├── sub_crt0.s          # Sub-CPU vector table & PRG-RAM startup
│   │   └── sub_main.c          # Word RAM 1M mode streaming & Ricoh PCM audio
│   └── everdrive/              # Mega EverDrive integration (assets/tower/everdrive/)
│       ├── everdrive.h         # EDIO register definitions
│       └── everdrive.c         # MicroSD sector streaming & USB edlink logging
├── assets/                     # Uncompressed PNGs, WAVs, and VGM v1.50 tracks
└── out/                        # Output binaries: tower_game.bin, tower_game.32x, tower_game_sub.bin
```

---

## Two-Tier Testing & Black-Screen Triage

### Tier 1: Host Logic Oracle
- Compile pure C core modules (`src/core/*.c`) on your development machine using system `gcc` or `clang`.
- Run unit test suites verifying math, physics, pathfinding, inventory, and state machines in milliseconds.
- Run `make test-host` before touching emulator builds.

### Tier 2: Headless Emulator Automation
- Drive **PicoDrive** (for 32X, Genesis, and CD) or **Genesis Plus GX** (for Genesis and CD) headlessly via libretro.
- Feed scripted controller inputs (`tests/scripts/boot.txt`, `menu.txt`, `gameplay.txt`).
- Capture framebuffers and run automated assertions:
  - Check non-zero pixel count (eliminates black screens).
  - Check color diversity (eliminates missing palette / single-color crash screens).
  - Check frame delta / animation (eliminates early lockups and deadlocks).

### Black-Screen Triage Ladder
If a ROM compiles but boots to a black screen, check causes in this exact order:
1. **RAM Overflow**: `.data + .bss` exceeded 256 KB SDRAM (32X), 68 KB Work RAM (Genesis), or 512 KB PRG-RAM (Sega CD).
2. **Palette Never Seeded**: Non-zero pixel data was drawn, but palette registers contain all zeros.
3. **Missing VBlank Synchronization**:
   - Genesis: Failed to call `SYS_doVBlankProcess()` at the end of each frame.
   - 32X: Frame buffer flip flag (`MARS_VDP_FS`) toggled without waiting for VBlank acknowledgment.
   - Sega CD: Word RAM bank swap requested without waiting for `RET` bit acknowledgment.
4. **Linker Dead Code Stripping (`--gc-sections`)**: Linker discarded game code because symbols were not marked `KEEP` or referenced from entry vector. Guarded by `verify_rom.py`.
5. **Cross-CPU Handshake Deadlock**: Master SH-2, 68000, or Sub-CPU spinning indefinitely on a COMM register that was never cleared.
6. **Odd-Address Word Access**: Motorola 68000 generates an address error exception when reading 16-bit or 32-bit values from odd memory addresses. Always ensure 16-bit word alignment.

---

## Bundled Documentation & Reference Guides

- `ANTIPATTERNS.md` — Catalog of explicit "Don't Do This" rules, hardware traps, compiler gotchas, and architectural antipatterns across Genesis, Sega CD, and 32X.
- `references/resources.md` — The complete SGDK `rescomp` specification, `.res` declarations (`IMAGE`, `SPRITE`, `TILESET`, `MAP`, `XGM2`, `WAV`, `BIN`), compression, and palette rules.
- `references/architecture.md` — Complete hardware architecture, memory maps, SSF mapper registers, dual SH-2 COMM protocols, 68000 branch limits, SRAM saves, integer sizes.
- `references/sega-cd.md` — Dedicated Sega CD guide: Sub-CPU programming, Word RAM 1M/2M modes, ASIC scaling and rotation, CD-ROM streaming, ISO creation.
- `references/audio-and-vgm.md` — Complete audio guide: SGDK XGM2 tooling (`xgm2tool`), VGM v1.50+ specification, Furnace/DefleMask composition, 32X PWM 8-voice mixer, slave ADPCM streaming, `midi2vgm` pipeline, Mega-CD Ricoh PCM & CD-DA.
- `references/asset-pipeline.md` — Decoupled intermediate asset pipeline (raw container → uncompressed PNG/WAV + JSON → baked 32X/Genesis/CD targets).
- `references/api-and-engine.md` — Complete runtime API: SGDK 2.x API (VDP, SPR, PAL, JOY, DMA, MAP, SYS, SRAM, fixed point) and 32X Mars hardware registers.
- `references/toolchain-and-build.md` — Complete toolchain guide: SGDK install routes (APT, Docker, MarsDev), LTO trap and library rebuilding, 32X dual-compiler setup, `romfix.py`, header generation.
- `references/porting-workflow.md` — The core/shell porting methodology, legacy system mapping tables (DOS/PC → Genesis/32X/CD), 7-step incremental bring-up order.
- `references/optimization.md` — Complete SH-2 and 68000 performance playbook: division elimination, reciprocal LUTs, dirty-rectangle rendering, 60/n VBlank quantization, sub-vblank headroom ballast testing.
- `references/testing.md` — Automated two-tier testing methodology: host logic oracle, headless Genesis Plus GX & PicoDrive libretro harnesses, input script DSL, PPM assertions, on-target SRAM markers, visual debugging probes.
- `references/software-3d.md` — Fixed-point software polygon 3D pipeline and flat-triangle rasterizer.
- `references/voxel-landscape.md` — Comanche-style voxel heightmap terrain engine.
- `references/2d-and-shmup.md` — 2D sprite rendering, scanline primitives, UI state machines.
- `references/strategy-and-grid.md` — A* pathfinding, fog of war, grid crawlers, deterministic game logic.
- `references/pico8-porting.md` — PICO-8 compatibility layer, palette mapping, resolution doubling.
- `references/fixed-point-math.md` — Hardware cycle cost model (68k, Z80, SH-2), Q16.16 arithmetic, SH-2 4-cycle MAC optimization (`dmuls.l` + `xtrct`), zero-RAM polynomial trigonometry, and minimal libfixmath usage.
- `references/examples.md` — Reference catalogue of real-world open source Genesis, 32X, and Sega CD ports, and SGDK 1.x → 2.x API migration table.
- `references/mega-everdrive.md` — Mega EverDrive PRO and CORE hardware and SDK reference: EDIO registers (`0xA130D0`), command framing, WRAM DMA halt trampoline, FAT MicroSD filesystem streaming, and host USB debugging with `edlink`.
- `references/mega-everdrive-edapp-and-fpga.md` — Mega EverDrive EDAPP external applications/file associations, `config.txt` specification, and custom FPGA mapper architecture (Quartus, SystemVerilog, MD+ streaming audio).
- `references/tower-of-power.md` — Complete Sega "Tower of Power" reference: 4-way hardware stack (Genesis + 32X + Sega CD + Mega EverDrive), 4-CPU concurrency model, boot handshake, Word RAM to 32X SDRAM graphics bridge, 4-way audio coordination, Docker workflows, and Git-LFS binary toolchain management.
- `references/vdp-graphics-and-effects.md` — Comprehensive Sega Genesis VDP graphics and visual effects guide: hardware priority ladder, line/column scrolling, Shadow & Highlight mode (153 colors), dither transparency, H-INT waterline raster effects, affine shearing, multi-jointed boss kinematics, and DMA bandwidth budgets.

---

## Author Credits & License Attribution

This skill combines, refines, and extends knowledge, templates, and tools created by the open-source Sega homebrew community:

- **libfixmath (`assets/fixmath/*`, `references/fixed-point-math.md`)**:
  - Copyright (c) 2011-2021 Flatmush `<Flatmush@gmail.com>`, Petteri Aimonen `<Petteri.Aimonen@gmail.com>`, and libfixmath AUTHORS (Chris Hammond, David Lechner, Gaëtan Harter, Joe Schaack, Martin Larralde, Stargirl Flowers, Vincent del Medico, Vitaly Puzrin, Xin Li, J.P. Hutchins).
  - Upstream: https://github.com/PetteriAimonen/libfixmath (MIT License).
- **Sega 32X GameDev Skill & Engines (`assets/3d/*`, `assets/2d/*`, `assets/harness.c`, `assets/run_tests.py`, `assets/romfix.py`, `assets/verify_rom.py`, `assets/scripts/*`)**:
  - Copyright (c) 2026 Haroldo de Oliveira Pinheiro `<haroldoop@gmail.com>` (haroldo-ok / `sega-32x-skill-for-claude`). (MIT License).
- **Sega Genesis SGDK Skill & Templates (`assets/main.c`, `assets/rom_header.c`, `assets/setup.sh`, `assets/Makefile.sgdk`, `assets/tests/*`)**:
  - Copyright (c) 2026 the sega-genesis-sgdk skill authors & Haroldo de Oliveira Pinheiro. (MIT License).
  - Headless test harness adapted from Haroldo de Oliveira Pinheiro's `jazz-jackrabbit-for-sega-genesis`, implementing Libretro API (The Libretro Team).
- **SGDK (Sega Genesis Development Kit)**:
  - Copyright (c) Stéphane Dallongeville `<stephane-d@numericable.fr>`.
  - Upstream: https://github.com/Stephane-D/SGDK (rescomp, xgm2tool, XGM2 audio driver, libmd runtime).
  - License: MIT License (library & custom tools); GCC/libgcc under GPLv3 with Runtime Library Exception.
- **32XDK & 32X Build Infrastructure (`assets/Makefile.32x`, `assets/mars.ld`)**:
  - Copyright (c) Joseph Groff ("Chilly Willy") and Victor Luchits ("Vic" / `viciious`).
  - Upstreams: https://github.com/viciious/32XDK, https://github.com/viciious/d32xr.
  - License: MIT License. Portions of DOOM 32X: Resurrection derived from Jaguar DOOM (subject to id Software source license) have been explicitly excluded and removed from this skill; only clean MIT build and linker infrastructure are included.
- **MarsDev Toolchain**:
  - Copyright (c) Andrew Andrianov ("andwn"). Upstream: https://github.com/andwn/marsdev (MIT License).
- **diablo32x Repository Contributions (`assets/bank_packer.py`, Two-Stage Decoupled Asset Pipeline Architecture)**:
  - Copyright (c) 2026 Dan Leslie `<dan@ironoxide.ca>` / diablo32x project contributors. (MIT License).
- **Mega EverDrive PRO/CORE SDK & Hardware Architecture (`references/mega-everdrive.md`, `references/mega-everdrive-edapp-and-fpga.md`)**:
  - Copyright (c) 2020-2026 Krikzz `<support@krikzz.com>` / Igor Golubovskiy. Upstream: https://github.com/krikzz/mega-ed-pub.
- **Community Research Notes & Specifications**:
  - High-level architectural notes (e.g. Sonic 1 Pattern Load Cues, Sega VR dual-CPU timing, DOOM 32X dual-SH2 rendering concepts) are included for educational reference only.
  - Sources from Nuked-OPN2, Furnace Tracker, Sonic 1 Disassembly, Sega VR Disassembly, and Jaguar DOOM are NOT included and have been explicitly excluded and removed from this repository and skill.
  - VGM Specification: Valley Bell & VGMrips (Open community technical specification).

See [LICENSE](file://.agents/skills/sega-32x-CD-genesis-gamedev/LICENSE) for the complete legal text.
