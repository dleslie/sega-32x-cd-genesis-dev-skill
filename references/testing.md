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
A lightweight standalone C program (~380 lines) that:
1. Dynamically loads the `.so` via `dlopen`.
2. Initializes libretro audio/video/input callbacks with headless dummy receivers.
3. Loads the target ROM.
4. Parses an input script DSL supporting holds, button mashing, snapshots, RAM dumps, audio energy checkpoints, and controller 2.
5. Dumps raw framebuffer frames as binary **PPM images** (`P6` format) for zero-dependency Python inspection.
6. Records work-RAM dumps (`.ram`) and optional audio captures (`HARNESS_WAV=output.wav`).

### C. Input Script DSL
The harness accepts comma-separated directives:

```text
120-160:start           hold START over frames 120..160
220-700:right           hold RIGHT over frames 220..700
420-424:b               tap B briefly
300-360:p2.left         hold LEFT on controller 2 (two-player / twin-stick)
mash@400-600:10:a+b     alternate A and B, each held for half of a 10-frame
                        period (get-up mashing, menu skipping, "press anything")
shot@110                write OUT_PREFIX110.ppm (a framebuffer snapshot)
ram@110                 write OUT_PREFIX110.ram (64 KB of 68000 work RAM)
audiostat@110           print "audiostat@110 samples=N avg_energy=E": mean
                        |sample| since previous audiostat (or boot)
```

Buttons: `up`, `down`, `left`, `right`, `a`, `b`, `c`, `start` (optionally prefixed with `p2.`).

For multi-stage playthroughs, scripts also exist as file-based recipes in `tests/scripts/*.txt`.

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

7. **Not a Crash Screen (`check_frames.py`):**
   - SGDK 2.x's default exception handlers reinitialise the VDP and print a register dump. Captured under Genesis Plus GX it is exactly four colours: black (~78%), cyan labels `(168,236,232)` (~18%), white values, and a yellow title line `(232,204,0)` ("ADDRESS ERROR", "BUS ERROR", "ILLEGAL INSTRUCTION").
   - `check_frames.py` keys on the cyan+yellow pair to prevent false positives on ordinary mostly-black text screens. The dump's `Offset:` line is the faulting PC; look it up in `out/release/symbol.txt`.
8. **Audio Energy Verification (`audiostat@`):**
   - Bracket frames that trigger sound effects or music with `audiostat@` checkpoints and assert energy jumps (e.g. 100–300× after SFX).
9. **Cadence (Streamed Video / FMV):**
   - For streaming animation or FMV, snapshot every Nth frame and verify each interval carries a new image.

---

## 5. Static ROM Verification (`verify_rom.py` & `rom_sanity.py`)

Static analysis executed during test runs:
- **`assets/tests/rom_sanity.py` (Cartridge Header Audit):**
  - Runs `python3 tests/rom_sanity.py out/rom.bin [--strict]`.
  - Asserts console name at `$100` starts with `"SEGA"` (TMSS compatibility).
  - Checks declared ROM end at `$1A4` against real file size (guards against hardware ILLEGAL INSTRUCTION traps).
  - Checks checksum at `$18E` across both formats:
    - *SGDK format:* XOR of every big-endian 32-bit word with checksum zeroed, folded to 16 bits (low ^ high). What `sizebnd -checksum` writes and `SYS_isChecksumOk()` checks.
    - *Classic Sega format:* 16-bit sum of big-endian words from `$200` to end (what emulators and flashcarts display).
  - Audits SRAM type, address bounds, and ROM aperture overlap warnings ($200000 window).
- **`verify_rom.py` (32X & Architecture Safety):**
  - Validates Mars module headers, SH-2 entry points, and power-of-two alignments.
  - Checks ELF section extents: `.text` in cartridge ROM, `.data`/`.bss` in SDRAM, and stack headroom guards (`__bss_end < 0x0603E000`).
  - Asserts string markers survive `--gc-sections` linker garbage collection.

---

## 6. Post-Mortems from RAM Dumps (`ramsym.py`)

When an emulator gives no debugger, `ram@<frame>` dumps combined with `assets/tests/ramsym.py` allow reading state post-mortem:
```sh
# Peek variables by symbol name (sizes in bytes, default 4; values big-endian)
python3 tests/ramsym.py out/frame_250.ram out/release/symbol.txt score:4 lives:1 game_state:1

# Scan top of RAM (stack) for plausible return addresses into code
python3 tests/ramsym.py out/frame_250.ram out/release/rom.out --stack 0xF000
```
- **Byte-Swapping Note:** Genesis Plus GX stores work RAM **byte-swapped within each 16-bit word** (e.g. `"Sir Lancelot"` reads as `"iS raLcnleto"` in raw dumps); `ramsym.py` swaps bytes by default (pass `--raw` to bypass).
- **Practical Uses:**
  - Verify `main()` was reached (store a magic value in a global on the first line of `main()`).
  - Read `MEM_getFree()` and DMA buffer pointers to diagnose boot-heap exhaustion.
  - Diagnose state machine locks and stack traces during hangs.
- **Deeper Profiling:**
  - *CPU registers per shot:* Hook `m68k_get_reg` in custom core builds to log PC, SR, A7, D0 at each snapshot.
  - *Cycle profiler:* Accumulate cycles per PC word to map execution hot spots to functions via `nm` and `addr2line`.

---

## 7. On-Target Self-Tests via Cartridge SRAM Markers

For deep internal invariants that cannot be observed from pixels alone:
- Compile with `-DSELFTEST`.
- Early boot code runs self-tests (table integrity, memory allocation limits, save deserialization) and writes a magic marker to cartridge SRAM (e.g., `'O', 'K', 1`).
- The test harness dumps SRAM upon exit and greps for the marker.
- **SRAM Interleave Gotcha:** On Sega Genesis hardware, SRAM is mapped to odd byte addresses. Emulator dumps (Genesis Plus GX) often interleave SRAM bytes with `0xFF`. The test suite must check for both contiguous `4F 4B 01`, `4F FF 4B FF 01`, and `FF 4F FF 4B FF 01`.

---

## 8. Hard-Won Testing & Debugging Gotchas

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
