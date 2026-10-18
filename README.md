# Sega Genesis, Sega CD, and 32X GameDev Skill

A complete development, porting, debugging, and verification skill for **Sega Genesis / Mega Drive**, **Sega CD / Mega-CD**, and **Sega 32X** (including **32X CD**, the **Tower of Power**, and **Standalone WebAssembly**).

This skill provides turnkey toolchains, graphics engines, sound drivers, fixed-point math, multi-CPU synchronization primitives, flashcart hardware extensions, and an automated two-tier test harness specifically designed to eliminate the primary failure mode of retro console development: **a ROM that compiles cleanly but boots to a black screen**.

---

## Supported Target Platforms

| Platform | Primary CPU(s) | Co-Processor / Graphics | Audio Hardware | Memory & Storage | Output Format |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Sega Genesis / Mega Drive** | Motorola 68000 @ 7.67 MHz | Z80 @ 3.58 MHz, VDP (2 planes, 80 sprites, Shadow/Highlight) | Yamaha YM2612 (6-ch FM) + TI SN76489 (4-ch PSG) | 64 KB WRAM, 64 KB VRAM, 8 KB Z80 RAM, up to 4 MiB ROM (or 32 MiB SSF) | `.bin` cartridge (SGDK 2.x) |
| **Sega CD / Mega-CD** | Genesis 68000 + Sub-68000 @ 12.5 MHz | ASIC (affine scaling/rotation, stamping), Word RAM (1M/2M) | CD-DA Redbook Audio (16-bit 44.1 kHz) + Ricoh RF5C164 (8-ch PCM) | +512 KB PRG RAM, +256 KB Word RAM, 64 KB PCM RAM, 540 MiB CD-ROM | `.iso` / `.chd` / `.cue`+`.bin` |
| **Sega 32X / Mars** | Dual Hitachi SH-2 @ 23 MHz (Master & Slave) | Genesis 68000 (I/O, timers), 32X VDP (dual framebuffers) | 12-bit Stereo PWM FIFO (~11–22 kHz) + Genesis FM/PSG | 256 KB SDRAM, 2×128 KB Framebuffer VRAM, cartridges up to 32 MiB (SSF) | `.32x` cartridge (32XDK) |
| **Sega 32X CD** | Dual SH-2 + Genesis 68000 + Sub-68000 | 32X VDP direct color overlaid on Genesis planes with ASIC rendering | 32X PWM + YM2612 + PSG + CD-DA + Ricoh PCM | Full combination: SDRAM + PRG RAM + Word RAM + WRAM + CD-ROM storage | Mixed CD-ROM image (`.iso`/`.chd`) + 32X boot |
| **Tower of Power** | Dual SH-2 + Main-68K + Sub-68K + Z80 | Mega EverDrive PRO/X7 (EDIO) + ASIC + 32X VDP | 4-Way Audio: YM2612 + PSG + Ricoh PCM + CD-DA + 32X PWM | Complete Stack: SDRAM + PRG RAM + Word RAM + MicroSD FAT streaming + USB `edlink` | Multi-binary stack (`.32x` + `.bin` + `.iso`) |
| **Standalone WebAssembly** | Browser Wasm (PicoDrive core) | Retro CRT shader shell, 60 Hz NTSC loop | WebAudio streaming (FM, PSG, PWM, PCM, CD-DA) | Client-side `localStorage` persistence, virtual SDRAM, VRAM, and WRAM | `.html` single-file (zero dependencies) |

---

## What Is Possible With This Skill

### 1. Turnkey Toolchains & Instant Zero-Root Setup
- **Hermetic Git-LFS Binaries**: GCC 14.2.0 cross-compilers for both Motorola 68000 (`m68k-elf`) and Hitachi SH-2 (`sh-elf`), SGDK 2.x, and MarsDev tools ship directly in `artifacts/`. Running `bash assets/setup.sh` unpacks them into `opt/` in ~3 seconds.
- **Rootless & Sandboxed**: Toolchains extract to `$(PROJECT_ROOT)/opt/` rather than `/opt/`, ensuring zero conflicts with system packages and 100% compatibility with CI runners and sandboxed environments.
- **Docker Integration**: Bundled `Dockerfile` and `assets/docker/run.sh` allow building any target in a clean, reproducible container with zero host installation.

### 2. Multi-System Graphics & Engine Capabilities
- **Software 3D Flat-Shaded Polygon Pipeline (`assets/3d/` - `r3d`)**: Complete lightweight 3D pipeline featuring matrix transformations, 3D clipping, projection, and flat-shaded triangle rasterization optimized for SH-2 and 68000.
- **Comanche-Style Voxel Heightmap Terrain**: High-performance heightmap voxel raycaster for rolling terrain and flight/driving landscapes (`references/voxel-landscape.md`).
- **2D Scanline & Sprite Engine (`assets/2d/`)**: Scanline primitives, sprite rendering, and UI state machines for arcade and shmup titles (`references/2d-and-shmup.md`).
- **Advanced Genesis VDP Visual Effects (`references/vdp-graphics-and-effects.md`)**:
  - Shadow & Highlight Mode: Display up to 153 simultaneous colors on stock Genesis hardware.
  - Per-line and per-column scrolling for multi-layer parallax backgrounds.
  - Horizontal interrupt (H-INT) raster effects for waterlines, sky gradients, and screen splits.
  - Dynamic Pattern Load Cues (PLC) to stream tile uploads across VBlanks without exceeding DMA bandwidth.
- **Sega CD Hardware ASIC Transformations (`references/sega-cd.md`)**:
  - Real-time arbitrary rotation (0° to 360°) and smooth continuous scaling (1/256× to 256×).
  - High-speed tile stamping into Word RAM in 1M Ping-Pong mode (Bank 0 renders while Bank 1 displays via Genesis VDP DMA at 60 FPS).
- **32X Direct-Color VDP Modes**:
  - Double-buffered framebuffers in 15bpp direct color (32,768 simultaneous colors) or 8bpp indexed (256/256 CRAM).

### 3. Open Video Game Music (VGM v1.50+) & Audio Stack
- **Standard VGM v1.50+ Runtime**: Author music in modern cross-platform trackers (**Furnace Tracker**, **DefleMask**) targeting the Genesis YM2612 FM + SN76489 PSG chips. Songs play via an interrupt-driven parser decoupled from game logic (`references/audio-and-vgm.md`).
- **SGDK XGM2 Sound Driver**: High-performance compiled sound driver using `xgm2tool` for 4-channel PCM mixing alongside 5-channel FM and PSG.
- **32X Stereo PWM Audio**: 12-bit stereo PWM audio mixer (~11–22 kHz). Supports streaming 4-bit IMA-ADPCM samples directly from cartridge ROM via the Slave SH-2 with zero SDRAM overhead.
- **Sega CD Studio Audio**: Zero-CPU Redbook CD-DA audio streaming directly from disc tracks, accompanied by the 8-channel 32 kHz Ricoh RF5C164 PCM chip with per-channel stereo panning.

### 4. Zero-FPU Fixed-Point Math (`libfixmath`)
- **Cycle Cost Model**: Eliminates software floating-point emulation (`__mulsf3`, `__divsf3`) which costs hundreds of cycles on FPU-less retro chips.
- **SH-2 Hardware MAC Acceleration**: Q16.16 multiplication executes in **4 clock cycles** on the SH-2 using `dmuls.l` and `xtrct` (25× faster than software float).
- **Embedded `libfixmath` (`assets/fixmath/`)**: Provides saturating Q16.16 arithmetic, pure 32-bit integer square roots, and zero-RAM polynomial trigonometry (`sin`, `cos`, `atan2`) (`references/fixed-point-math.md`).
- **Reciprocal LUTs & Division Elimination**: Division replaced with multiplication by reciprocal lookup tables, branch elimination, and dirty-rectangle rendering (`references/optimization.md`).

### 5. Extended Storage & Mega EverDrive Integration
- **Sega SSF Banked Cartridge Mapper (Up to 32 MiB)**: Exceed the 4 MiB Genesis ROM boundary up to 32 MiB using 512 KiB switchable apertures, managed via `assets/bank_packer.py` with automatic SH-2 cache purging (`references/architecture.md`).
- **Mega EverDrive PRO / X7 / CORE Support**:
  - Hardware EDIO register access (`0xA130D0`) for direct MicroSD FAT filesystem streaming (`assets/tower/everdrive/`).
  - USB host logging and run-time debugging with `edlink` (`references/mega-everdrive.md`).
  - Custom FPGA mappers (Quartus/SystemVerilog) and EDAPP external applications (`references/mega-everdrive-edapp-and-fpga.md`).

### 6. Standalone Single-File WebAssembly (.html) Distribution
- **Zero-Server Distribution**: Compiles any project into a standalone single-file `.html` application via Emscripten (`-s SINGLE_FILE=1`). The binary, embedded PicoDrive core, retro CRT shader shell, and ROM image are unified into one file (`assets/wasm/`).
- **Copy-and-Play**: Open directly via `file:///path/to/game.html`, attach to chat, send via email, or copy to a USB drive—no web server or backend needed.
- **Client-Side Persistence**: Battery SRAM and quick-save states (F5 save / F8 restore) persist directly to browser `localStorage` with FNV-1a dirty hashing.
- **Live Hardware Telemetry HUD**: Press **F2** or backtick (**`**) to inspect:
  - Memory Subsystems: Live virtual Wasm heap (`sbrk`), 32X SDRAM, Framebuffers, M68K RAM, VDP VRAM, and Z80 RAM bars.
  - CPU Program Counters: Live Master & Slave SH-2 `PC` registers and state badges (`RUN`, `SLEEP`, `CPOLL`, `VPOLL`, `RPOLL`).
  - Inter-CPU COMM Bus: Real-time monitor of `COMM0` through `COMM14` registers.
  - Frame Pacing & Audio: Real-time microsecond core execution time (< 16,667 µs for 60 FPS) and audio ring buffer fill percentage.

### 7. Two-Tier Verification & Black-Screen Prevention
- **Tier 1: Host Logic Oracle (`assets/tests/run_host_tests.sh`)**: Compiles portable C core modules on host `gcc`/`clang` to run fast unit test suites verifying math, physics, pathfinding, and state machines in milliseconds.
- **Tier 2: Headless Emulator Automation (`assets/tests/run_all.sh`)**: Drives PicoDrive or Genesis Plus GX headlessly via libretro. Executes scripted joypad input sequences (`assets/scripts/01_boot.txt`, `02_menu.txt`, `03_gameplay.txt`) and asserts non-black, colorful, and moving frames (`check_frames.py`).
- **Post-Link ROM Validator (`assets/verify_rom.py`, `assets/romfix.py`)**: Checks entry vectors, memory limits, symbol stripping (`KEEP`), and header checksums before deploying to hardware.
- **Black-Screen Triage Ladder & Antipatterns**: Complete catalog of hardware traps, compiler gotchas, and architectural antipatterns (`references/ANTIPATTERNS.md`).

### 8. Core / Shell Porting Framework
- **Decoupled Two-Stage Asset Pipeline**: Raw proprietary source archives (WAD, PAK, DOS assets) are converted into standard uncompressed intermediate assets (PNG, WAV, VGM v1.50) via `make extract-assets`. Normal `make` then packs these into console-native formats (`rescomp`, packed bitmaps, ISO) (`references/asset-pipeline.md`).
- **Porting Playbooks**: Step-by-step guides for porting DOS/PC games, Arcade engines, grid/strategy crawlers (`references/strategy-and-grid.md`), and PICO-8 titles (`references/pico8-porting.md`).

---

## Prerequisites & Installation

### 1. Prebuilt Git-LFS Toolchains (Instant)
Unpack complete GCC 14.2.0 cross-compilers (`m68k-elf` and `sh-elf`) plus SGDK 2.x into `opt/` in ~3 seconds:
```bash
git lfs pull
bash assets/setup.sh
```

### 2. Docker Support (Zero Host Toolchains)
Build cleanly inside the self-contained container without installing local cross-compilers:
```bash
./assets/docker/run.sh make -f assets/Makefile.tower
```

### 3. Agent Skill Installation
Symlink or copy this repository into your agent's skill directory:
```bash
# Antigravity CLI / User skills
ln -s "$PWD" ~/.gemini/antigravity-cli/skills/sega-32x-CD-genesis-gamedev

# Project-local skills
mkdir -p .agents/skills
ln -s "$PWD" .agents/skills/sega-32x-CD-genesis-gamedev
```

---

## Quickstart & Project Scaffolding

### A. Sega Genesis / Mega Drive (SGDK 2.x)
```bash
cp assets/Makefile.sgdk Makefile
cp assets/main.c src/main.c
cp assets/rom_header.c src/rom_header.c
make
# Produces out/rom.bin
```

### B. Sega 32X (32XDK)
```bash
cp assets/Makefile.32x Makefile
cp assets/mars.ld src/platform/32x/mars.ld
make
# Produces rom/game.32x
```

### C. Sega "Tower of Power" (Genesis + 32X + Sega CD + EverDrive)
```bash
make -f assets/Makefile.tower
# Or via Docker:
./assets/docker/run.sh make -f assets/Makefile.tower
# Produces out/tower_game.bin, out/tower_game.32x, and out/tower_game_sub.bin
```

### D. Standalone Single-File WebAssembly (.html)
```bash
make -f assets/wasm/Makefile.wasm wasm ROM=rom/game.32x
# Produces dist/game.html (open directly in any browser: file:///path/to/dist/game.html)
```

---

## Verification & Testing Pipeline

To ensure a game not only compiles but is fully playable without black screens:

1. **Host Logic Oracle** (fast unit tests for pure C core logic):
   ```bash
   bash assets/tests/run_host_tests.sh
   ```
2. **Post-Link ROM Check** (validates entry vectors, symbol retention, memory bounds):
   ```bash
   python3 assets/verify_rom.py out/rom.bin
   ```
3. **Headless Emulator Smoke Test** (asserts non-black, colourful, changing frames with scripted inputs):
   ```bash
   bash assets/tests/run_all.sh
   # Or run automated frame-check tests directly:
   python3 assets/run_tests.py
   ```

---

## Acknowledgments & Upstream Credits

This project synthesizes, refines, and extends work from the Sega homebrew community:

- **[sega-genesis-sgdk-skill-for-claude](https://github.com/haroldo-ok/sega-genesis-sgdk-skill-for-claude)** & **[sega-32x-skill-for-claude](https://github.com/haroldo-ok/sega-32x-skill-for-claude)** by Haroldo de Oliveira Pinheiro (`haroldo-ok`).
- **[SGDK](https://github.com/Stephane-D/SGDK)** by Stéphane Dallongeville (rescomp, xgm2tool, XGM2 driver, libmd).
- **[32XDK](https://github.com/viciious/32XDK)** & **[d32xr](https://github.com/viciious/d32xr)** by Joseph Groff ("Chilly Willy") and Victor Luchits ("Vic" / `viciious`).
- **[MarsDev](https://github.com/andwn/marsdev)** by Andrew Andrianov ("andwn").
- **[libfixmath](https://github.com/PetteriAimonen/libfixmath)** by Flatmush, Petteri Aimonen, and contributors.
- **[Mega EverDrive SDK](https://github.com/krikzz/mega-ed-pub)** by Igor Golubovskiy ("Krikzz").
- **[Libretro / PicoDrive / Genesis Plus GX](https://github.com/libretro)** for emulation cores and test harness foundations.
- **[diablo32x](https://github.com/dleslie/diablo32x)** contributions by Dan Leslie (two-stage decoupled asset pipeline architecture, SSF bank packer).

See [LICENSE](LICENSE) for complete copyright notices and third-party license terms.
