# Automated Two-Tier Testing & Emulation Verification Reference

"It compiled" is never "it works" on Sega hardware. The default failure mode of a naive Genesis, Sega CD, or 32X build is a **syntactically valid ROM that boots to a black screen**. This reference details the automated two-tier testing methodology, static ROM verification, headless emulator harnesses, frame assertion heuristics, and debugging probes.

---

## 1. The Two-Tier Testing Architecture

```
                    ┌────────────────────────────────────────┐
                    │      Portable Core (src/core/)         │
                    └───────────────────┬────────────────────┘
                                        │
                 ┌──────────────────────┴──────────────────────┐
                 ▼                                             ▼
     ┌───────────────────────┐                     ┌───────────────────────┐
     │  TIER 1: Host Oracle  │                     │ TIER 2: Headless Emu  │
     │  Fast, pure C logic   │                     │ Real ROM, boots emu   │
     │  No emu, ASan/Valgrind│                     │ Frame & SRAM asserts  │
     └───────────────────────┘                     └───────────────────────┘
```

---

## 2. Tier 1: Host Logic Oracle (Fast, Deterministic)

Compile the platform-clean core (`src/core/`) using the host compiler (`gcc` / `clang`), linking against a synthetic test harness:
- **Zero Emulator Overhead:** Runs in milliseconds on local developer machines and CI pipelines.
- **Full Debugger & Sanitizer Support:** Debuggable via `gdb`, `lldb`, AddressSanitizer (`-fsanitize=address`), and `valgrind`.
- **Scope:** Physics calculation, collision bounds, inventory state machines, combat math, procedural level generation reachability (BFS), quest triggers, and asset converters (verifying decoders against synthetic files).
- **Compilation Pattern:**
  ```sh
  cc -std=c11 -O2 -Wall -Wextra -Isrc/core \
     src/core/dungeon.c src/core/actor.c src/core/loot.c \
     tests/host/test_combat.c -o build/test_combat
  ./build/test_combat
  ```
  Guard console-specific HAL calls with `#ifndef HOST_BUILD`.

---

## 3. Tier 2: Headless Emulator Harnesses (Genesis & 32X)

Tier 2 boots the actual produced binary (`out/rom.bin` or `rom/game.32x`) under a headless libretro emulator core, injects scripted controller inputs, captures framebuffer snapshots (PPM format), and evaluates pixel assertions.

### A. Core Selection & Compilation
- **Genesis / Sega CD:** **Genesis Plus GX** (`ekeeke/Genesis-Plus-GX`).
  ```sh
  git clone --depth 1 https://github.com/ekeeke/Genesis-Plus-GX.git
  make -C Genesis-Plus-GX -f Makefile.libretro platform=unix -j"$(nproc)"
  # Produces genesis_plus_gx_libretro.so
  ```
- **Sega 32X / 32X CD:** **PicoDrive** (`libretro/picodrive`).
  ```sh
  git clone --depth 1 --recurse-submodules https://github.com/libretro/picodrive.git
  make -C picodrive -f Makefile.libretro platform=unix -j"$(nproc)"
  # Produces picodrive_libretro.so
  ```

### B. Headless C Harness (`harness.c` / `libretro_harness.c`)
A lightweight standalone C program (~250 lines) that:
1. Dynamically loads the `.so` via `dlopen`.
2. Initializes libretro audio/video/input callbacks with headless dummy receivers.
3. Loads the target ROM.
4. Parses an input script DSL.
5. Dumps raw framebuffer frames as binary **PPM images** (`P6` format) for zero-dependency Python inspection.

### C. Input Script DSL
Scripts reside in `tests/scripts/*.txt` as readable sequence commands:

| Command | Arguments | Effect |
| :--- | :--- | :--- |
| `run` | `<frames>` | Advances emulation by `<frames>` with neutral controls. |
| `press` | `<button> <frames>` | Holds `<button>` for `<frames>`, then releases. |
| `hold` | `<button>` | Sticky press of `<button>` until explicit release. |
| `release` | `<button>` | Releases `<button>`. |
| `port` | `<0\|1>` | Switches target controller port (Port 1 or Port 2). |
| `shot` | `<name>` | Writes current framebuffer snapshot to `<outdir>/<name>.ppm`. |

**Buttons Supported:** `up`, `down`, `left`, `right`, `a`, `b`, `c`, `start`, `x`, `y`, `z`, `mode`.

*Example Script (`tests/scripts/02_menu.txt`):*
```text
run 30
shot boot_title
press start 5
run 20
shot menu_open
press down 5
run 15
shot menu_cursor_down
press start 5
run 60
shot gameplay_active
```

---

## 4. Headless Frame Assertions & Heuristics

`tests/run_tests.py` analyzes the dumped PPM frames:

1. **Not Black (`visible_ratio`):**
   - Counts non-black pixels. If >92% of the frame is black (or the dominant color), the test fails.
   - Formula: $\text{visible\_ratio} = 1.0 - \frac{\text{dominant\_color\_count}}{\text{total\_pixels}} > 0.08$.
2. **Colourful (`distinct_colors`):**
   - Measures unique RGB colors. Catches uninitialized or zeroed palettes (where all indices map to black).
   - Threshold: $\ge 8$ distinct colors for 2D scenes, $\ge 6$ for flat-shaded 3D scenes.
3. **Changing / Animating:**
   - Consecutive checkpoint frames must differ (CRC32 or pixel delta $> 0$), proving simulation is not frozen.
4. **State Transitions:**
   - Snapshots across game phases (Boot $\to$ Title $\to$ Gameplay) must have distinct visual signatures.
5. **Bounded Input Effect (Menu Regression Guard):**
   - Pressing `Down` in a menu must alter cursor pixels ($0.0001 < \Delta < 0.08$) without violently jumping screens (guarding against 6-button pad mirroring bugs).
6. **Region Crops:**
   - Evaluates sub-rectangles (e.g., player HP orb, HUD belt slots) for expected colors, detecting missing sprites or corrupted local palette rows.

---

## 5. Static ROM Verification (`verify_rom.py`)

Static analysis executed directly during `make check`:
- **Cartridge Header Validation:**
  - Standard Genesis header at `0x100`: `"SEGA GENESIS"` or `"SEGA 32X"`.
  - Mars module header present; valid SH-2 Master/Slave entry points.
  - Cartridge size matches real file size; power-of-two alignment.
  - 16-bit big-endian checksum at `0x18E` matches recomputed word sum from `0x200`.
- **ELF Section Extents & Budget Safety:**
  - `.text` resides in cartridge ROM aperture (`0x02000000..0x023FFFFF`).
  - `.data` and `.bss` reside in SDRAM (`0x06000000`).
  - **Headroom Guard:** Asserts `__bss_end < 0x0603E000`, ensuring at least 8 KiB of SDRAM remains for runtime stack frames.
- **Linked-Code String Markers:**
  - Asserts that known string literals from disparate translation units exist in the binary.
  - Detects aggressive linker stripping (`--gc-sections`) that leaves only boot headers while stripping game code.

---

## 6. On-Target Self-Tests via Cartridge SRAM Markers

For deep internal invariants that cannot be observed from pixels alone:
- Compile with `-DSELFTEST`.
- Early boot code runs self-tests (table integrity, memory allocation limits, save deserialization) and writes a magic marker to cartridge SRAM (e.g., `'O', 'K', 1`).
- The test harness dumps SRAM upon exit and greps for the marker.
- **SRAM Interleave Gotcha:** On Sega Genesis hardware, SRAM is mapped to odd byte addresses. Emulator dumps (Genesis Plus GX) often interleave SRAM bytes with `0xFF`. The test suite must check for both contiguous `4F 4B 01` and interleaved `4F FF 4B FF 01`.

---

## 7. Hard-Won Testing & Debugging Gotchas

### ⚠️ Never Inject Controller Input on Frame 0
Hardware and emulators require 20–30 frames to reset hardware, execute boot ROM sequences, and initialize the VDP. Injecting `press start` at frame 0 will be silently ignored. Always execute `run 30` before injecting inputs.

### ⚠️ Libretro Button Mapping Discrepancies
Through the libretro PicoDrive core:
- Script `a` maps to **Genesis C**.
- Script `b` maps to **Genesis B**.
- Script `c` maps to **Genesis A**.
When testing fire/jump actions, allow `A | B | C` or calibrate inputs empirically.

### ⚠️ "Run N" Emulated Frames $\ne$ N Simulation Ticks
Under heavy rendering workloads, games running at 20 fps advance their internal simulation once every 3 emulated frames. If an enemy spawn timer is 40 ticks, it will not appear after 40 emulated frames; it requires 120 emulated frames. Calibrate waits accordingly.

### ⚠️ Visual Probes for Headless Debugging
When an emulator gives only pixels without a debugger:
- **State-to-Column Marker:** Encode variable values into horizontal tile coordinates on a dedicated HUD row:
  ```c
  VDP_setTileMapXY(BG_A, marker_tile, current_state_value, 0);
  ```
- **RAM Persistence Probe:**
  ```c
  static uint16_t g_probe = 0; g_probe++;
  ```
  Draw `g_probe` as a tile marker. If it fails to advance on-screen, RAM writes are failing (signaling an LTO or memory initialization bug).
- **Unconditional Draw:** When an actor "does not appear", force it to render unconditionally at fixed screen coordinates $(100, 100)$. If it draws, the render path is healthy and the bug is in AI/spawning; if not, the bug is in the renderer or palette.
