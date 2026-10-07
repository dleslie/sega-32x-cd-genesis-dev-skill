# Sega Genesis, Sega CD, and 32X GameDev Skill

Development and porting skill for Sega Genesis / Mega Drive, Sega CD / Mega-CD, and Sega 32X (including 32X CD).

## Supported Targets

| Platform | Primary CPU | Co-Processor / Graphics | Audio | Output Format |
| :--- | :--- | :--- | :--- | :--- |
| **Genesis / Mega Drive** | 68000 @ 7.67 MHz | Z80 @ 3.58 MHz, VDP (2 planes, 80 sprites) | YM2612 (FM) + PSG | `.bin` cartridge (SGDK 2.x) |
| **Sega CD / Mega-CD** | 68000 + Sub-68000 @ 12.5 MHz | ASIC (scaling/rotation), Word RAM (1M/2M) | Ricoh RF5C164 (PCM) + CD-DA | `.iso` / `.chd` / `.cue`+`.bin` |
| **Sega 32X / Mars** | Dual SH-2 @ 23 MHz | Genesis 68000, 32X VDP (double-buffered framebuffers) | 12-bit Stereo PWM + YM2612 | `.32x` cartridge (32XDK) |
| **Sega 32X CD** | Dual SH-2 + Genesis 68000 | Sub-68000, ASIC, 32X VDP + Genesis VDP | PWM + PCM + CD-DA + FM/PSG | Mixed CD-ROM image + 32X boot |
| **Tower of Power** | Dual SH-2 + Main-68K + Sub-68K + Z80 | Mega EverDrive PRO/X7 (EDIO) + ASIC | 4-Way Audio: YM2612 + PSG + Ricoh PCM + CD-DA + 32X PWM | `.32x` cart + `.bin` + `.iso` disc |

## Repository Structure

- `SKILL.md` — Core instructions, architecture specifications, and porting workflows.
- `ANTIPATTERNS.md` — Catalog of hardware traps, compiler gotchas, and fatal antipatterns.
- `artifacts/` — Prebuilt GCC 14.2.0 cross-compilers (`m68k-elf` and `sh-elf`) & SGDK 2.x tracked with **Git LFS**.
- `Dockerfile` & `assets/docker/run.sh` — Self-contained development container (`sega-tower-dev`).
- `references/` — Technical reference guides (architecture, tower-of-power, audio/VGM, asset pipeline, Sega CD, fixed-point math, 3D, optimization, testing, Mega EverDrive SDK).
- `assets/` — Reusable engines, templates, and tooling:
  - `setup.sh` — Toolchain setup script (Git-LFS artifacts, Docker, MarsDev, APT).
  - `Makefile.tower` — Canonical multi-architecture build for the complete Tower of Power stack.
  - `Makefile.sgdk`, `Makefile.32x`, `mars.ld` — Dedicated Genesis and 32X standalone build systems.
  - `tower/` — Bootstrapping, inter-CPU handshakes, and reference main (`boot/`, `sh2/`, `sub_68k/`, `everdrive/`).
  - `fixmath/` — Standalone Q16.16 libfixmath (integer sqrt, polynomial trig).
  - `3d/` — Lightweight software 3D flat-shaded polygon rasterizer (`r3d`).
  - `bank_packer.py` — Sega SSF bank packer for cartridges > 4 MiB (up to 32 MiB).
  - `verify_rom.py`, `romfix.py` — Post-link ROM header, symbol, and checksum validators.
  - `tests/` — Headless libretro (PicoDrive / Genesis Plus GX) test harness.

## Prerequisites & Installation

- **Prebuilt Git-LFS Toolchains (Instant)**:
  ```bash
  git lfs pull
  bash assets/setup.sh
  ```
  Unpacks complete GCC 14.2.0 cross-compilers for both Motorola 68000 (`m68k-elf`) and Hitachi SH-2 (`sh-elf`) plus SGDK 2.x into `opt/` in ~3 seconds.
- **Docker Support**:
  Build any target inside the container without modifying host packages:
  ```bash
  ./assets/docker/run.sh make -f assets/Makefile.tower
  ```
- **Host Tests**: Host C compiler (`gcc`/`clang`), Python 3.
- **Headless Tests**: Headless libretro core (`picodrive_libretro.so` or `genesis_plus_gx_libretro.so`).

Toolchains and SDKs are extracted to `$(PROJECT_ROOT)/opt` rather than system `/opt` to maintain self-contained, unprivileged builds.

## Usage

### 1. Agent Skill Installation

Symlink or copy this repository into your agent's skill directory:

```bash
# Antigravity CLI / User skills
ln -s "$PWD" ~/.gemini/antigravity-cli/skills/sega-32x-CD-genesis-gamedev

# Project-local skills
mkdir -p .agents/skills
ln -s "$PWD" .agents/skills/sega-32x-CD-genesis-gamedev
```

The skill triggers on requests involving Sega Genesis, Sega CD, or 32X game development, porting, debugging, or optimization.

### 2. Project Scaffolding

Bootstrap projects using the templates in `assets/`:

- **Sega Tower of Power (Genesis + 32X + Sega CD + EverDrive)**:
  ```bash
  make -f assets/Makefile.tower
  # Or via Docker:
  ./assets/docker/run.sh make -f assets/Makefile.tower
  ```

- **Genesis (SGDK)**:
  ```bash
  cp assets/Makefile.sgdk Makefile
  cp assets/main.c src/main.c
  cp assets/rom_header.c src/rom_header.c
  make
  ```

- **Sega 32X (32XDK)**:
  ```bash
  cp assets/Makefile.32x Makefile
  cp assets/mars.ld src/platform/32x/mars.ld
  make
  ```

### 3. Verification & Testing

Follow the two-tier verification pipeline to prevent black screens:

1. **Host Logic Oracle** (fast unit tests for pure C core logic):
   ```bash
   bash assets/tests/run_host_tests.sh
   ```
2. **Post-Link ROM Check** (validates entry vectors, symbol retention, memory bounds):
   ```bash
   python3 assets/verify_rom.py out/rom.bin
   ```
3. **Headless Emulator Smoke Test** (asserts non-black, colourful, changing frames):
   ```bash
   bash assets/tests/run_all.sh
   # Or individually:
   python3 assets/run_tests.py
   ```

## Acknowledgments

This project is a modification, merger, and extension of the following existing skill projects:

- [sega-genesis-sgdk-skill-for-claude](https://github.com/haroldo-ok/sega-genesis-sgdk-skill-for-claude)
- [sega-32x-skill-for-claude](https://github.com/haroldo-ok/sega-32x-skill-for-claude)

See [LICENSE](LICENSE) for complete third-party attribution and copyright notices.

