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

## 3. Tier 2: Headless `ares` Emulator Harness (MD / 32X / Mega CD / MCD32X)

Tier 2 boots the actual produced binary (`out/rom.bin`, `rom/game.32x`, or `.chd`/`.cue`/`.iso`) under headless **`ares`** (`opt/ares/ares-harness`), injects scripted controller inputs, captures framebuffer snapshots (24-bit BMP in script mode, P6 PPM by default in `--inline` mode), and evaluates pixel/SDRAM/audio assertions.

### A. Unified `ares-harness` & Pure SH-2 Interpreter Default
- Prebuilt static-dependency binary bundled in `artifacts/ares-headless.tar.gz` (`opt/ares/ares-harness`, built from `assets/ares/build_ares_headless.sh` at `ares` commit `6e67c8bd`, ISC License).
- Includes built-in clean `mia` Mega 32X boot/vector ROMs (`sh2.boot.mrom`, `sh2.boot.srom`, `vector.rom`). Pass `--bios <path>` when testing Mega CD / Mega CD 32X discs.
- **Accuracy Default (`"Recompiler" = false`)**: Defaults to pure SH-2 interpreter mode so SH7604 cache purging (`CCR`), `RV` bus arbitration, active-display `CRAM` writes, VBlank `0x18E` checksum loops, and true 5-bit `BGR555` color scaling (`(v << 3) | (v >> 2)`) match real hardware. Pass `--recompiler` or set `ARES_RECOMPILER=1` only for ad-hoc speed experiments (never in regression gates).

### B. Modes (`assets/ares/ares_harness.cpp`)
1. **Script mode**: `ares-harness [opts] <rom> <script.txt> <outdir>` (executes `run`, `press`, `hold`, `release`, `port`, `shot`, `audiostat`, `sample_prof` -> dumps `<shot>.bmp`, `audio.txt`, and SDRAM profile `.bin` slices).
2. **Inline mode**: `ares-harness --inline [opts] <rom> <total_frames> <directives> <out_prefix>` (comma-separated holds, `mash@`, `shot@`, `ram@`, `audiostat@` -> dumps `.ppm` frames by default plus big-endian `.ram` dumps).
3. **32X boot probe**: `ares-harness --comm-check [opts] <rom.32x> [frames]` (steps `frames` [default `180`], prints pixel lit ratio and `COMM0..COMM14`, fails if blank).

### C. Input Script DSL (Inline Mode)
In `--inline` mode, the harness accepts comma-separated directives:

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
   - SGDK 2.x's default exception handlers reinitialise the VDP and print a register dump in four colours: black (~78%), cyan labels `(168,236,232)` (~18%), white values, and a yellow title line `(232,204,0)` ("ADDRESS ERROR", "BUS ERROR", "ILLEGAL INSTRUCTION").
   - `check_frames.py` supports both P6 `.ppm` and 24-bit `.bmp` frames and keys on the cyan+yellow pair to prevent false positives on ordinary mostly-black text screens. The dump's `Offset:` line is the faulting PC; look it up in `out/release/symbol.txt`.
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
- **Byte-Swapping Note:** `ares-harness` dumps 68000 work RAM directly in **big-endian 68000 byte order**; pass `--raw` to `ramsym.py` (`python3 tests/ramsym.py out/frame_250.ram out/release/rom.out --raw ...`). Legacy Genesis Plus GX libretro dumps byte-swapped each 16-bit word, which remains `ramsym.py`'s default when `--raw` is omitted.
- **Practical Uses:**
  - Verify `main()` was reached (store a magic value in a global on the first line of `main()`).
  - Read `MEM_getFree()` and DMA buffer pointers to diagnose boot-heap exhaustion.
  - Diagnose state machine locks and stack traces during hangs.

---

## 7. On-Target Self-Tests via Cartridge SRAM Markers

For deep internal invariants that cannot be observed from pixels alone:
- Compile with `-DSELFTEST`.
- Early boot code runs self-tests (table integrity, memory allocation limits, save deserialization) and writes a magic marker to cartridge SRAM (e.g., `'O', 'K', 1`).
- The test harness dumps SRAM upon exit (`--dump-sram <path>` in `ares-harness`) and checks for the marker.
- **SRAM Interleave Gotcha:** On Sega Genesis hardware, odd-byte-mapped SRAM stores every byte at odd addresses (`0x200001, 0x200003, ...`). Check for contiguous `4F 4B 01` as well as `0xFF`-interleaved forms (`4F FF 4B FF 01` / `FF 4F FF 4B FF 01`) depending on how the emulator formats `.ram` save files.

---

## 8. Hard-Won Testing & Debugging Gotchas

### ⚠️ Never Inject Controller Input on Frame 0 (or Too Early on Real 32X Boot)
Hardware and accurate emulators (`ares`) execute both the Genesis/32X boot ROM sequence and engine initialization before entering menus. For standard Genesis ROMs wait at least `run 30`; for 32X banked cartridges with SDRAM/asset init wait ~95–120 frames (`run 120`) before injecting first menu inputs. Note also that real 32X boot ROM sums words up to `4 MiB` at boot when `0x18E != 0`; `>4 MiB` `"SEGA SSF"` ROMs should write `0x0000` at `0x18E` (`assets/romfix.py`) to avoid a multi-second boot stall.

### ⚠️ Button Mapping & True BGR555 Color Accuracy (`ares` vs Libretro)
- `ares-harness` maps script button names (`a`, `b`, `c`, `start`, `x`, `y`, `z`, `mode`) 1:1 to `ares` `FightingPad` inputs with zero libretro RetroPad remapping.
- `ares` (`VDP::color`) expands 5-bit 32X CRAM channels via bit-replication `(v << 3) | (v >> 2)`, whereas RGB565-based cores (such as PicoDrive WASM) truncate or rescale green (`28 -> 226` vs true `231`). Anchor pixel assertions against true 5-bit BGR555 values (`255, 231, 165` etc.).

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
