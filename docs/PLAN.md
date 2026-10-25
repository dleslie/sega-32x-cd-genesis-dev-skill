# Development Plan: Testing Automation, Sega CD Mastering, CI/CD, & WebAssembly

This document details the roadmap and execution strategy for expanding and hardening the Sega development skill, covering headless emulator integration, turnkey Sega CD disc mastering, automated CI pipelines, and standalone WebAssembly runtime bundling.

---

## Phase 1: Wire Prebuilt Artifacts into the Test Harnesses (Completed)

### Goal
Eliminate external network calls and source-compilation overhead during local and CI test execution by automatically consuming the prebuilt libretro cores in `opt/libretro/`.

### Work Items
1. **Update `assets/tests/run_headless_emulator_test.sh`**:
   - Check for prebuilt core in `$OPT_DIR/libretro/genesis_plus_gx_libretro.so` or `$PROJECT_ROOT/opt/libretro/genesis_plus_gx_libretro.so`.
   - Use the prebuilt core when present before falling back to cloning and building `ekeeke/Genesis-Plus-GX`.
2. **Expand `assets/tests/run_all.sh`**:
   - Run Tier 1 host tests (`run_host_tests.sh`).
   - Run Tier 2 Genesis headless tests (`run_headless_emulator_test.sh`).
   - Add Tier 2 32X headless test execution via `assets/run_tests.py` using `opt/libretro/picodrive_libretro.so`.
3. **Verify Execution**:
   - Run the complete test suite locally and assert all tiers pass cleanly.

---

## Phase 2: Sega CD Turnkey Build & Disc Mastering Pipeline (Completed)

### Goal
Provide an automated, zero-config disc mastering workflow that turns Sub-68K and Main-68K binaries into bootable ISO-9660 and compressed CHD disc images using the bundled `cd-mastering-tools.tar.gz`.

### Work Items
1. **Mastering Script (`assets/build_disc.sh`)**:
   - Accepts data track binary, staging assets directory, and optional CD-DA WAV tracks.
   - Invokes `opt/bin/genisoimage` to generate `track01.iso` with Sega CD boot header injected into the System Area (sectors 0-15).
   - Generates `.cue` sheet pairing data and audio tracks.
   - Invokes `opt/bin/chdman createcd` to emit a compressed `.chd` image.
2. **Dedicated `assets/Makefile.cd`**:
   - Builds Main-CPU 68000 binary (`main.bin`) and Sub-CPU 68000 binary (`sub.bin`).
   - Links with disc mastering script into `out_cd/game.chd`, `out_cd/game.cue`, and `out_cd/game_track01.iso`.
3. **Standalone Dual-68K Starter Template**:
   - Minimal Main-CPU / Sub-CPU handshake in `assets/cd/src/main_68k/main_cd.c` and `assets/cd/src/sub_68k/sub_main.c`.
   - 1M Word RAM ping-pong graphics buffer swapping and Ricoh PCM initialization.
   - Complete documentation in `assets/cd/README.md`.

---

## Phase 3: Continuous Integration (GitHub Actions Matrix) (Next)

### Goal
Establish an automated CI workflow that tests every push across all supported target platforms and verification tiers.

### Work Items
1. **Create `.github/workflows/ci.yml`**:
   - Runner: `ubuntu-latest`.
   - Steps:
     - Check out repo with Git LFS (`actions/checkout` with `lfs: true`).
     - Run `bash assets/setup.sh` to extract toolchains into `opt/`.
     - Compile Genesis starter ROM (`make -f assets/Makefile.sgdk`).
     - Compile 32X starter ROM (`make -f assets/Makefile.32x`).
     - Compile Tower of Power stack (`make -f assets/Makefile.tower`).
     - Run `bash assets/tests/run_all.sh` asserting all host logic and headless emulator smoke tests pass.

---

## Phase 4: Standalone WebAssembly Runtime & Verification

### Goal
Make single-file HTML WebAssembly export completely turnkey and verify telemetry and persistence capabilities.

### Work Items
1. **Bundle WebAssembly Runtime Library**:
   - Package prebuilt `libpicodrive_wasm.a` and headers into `artifacts/picodrive-wasm.tar.gz`.
2. **Test Single-File HTML Generation**:
   - Run `make -f assets/wasm/Makefile.wasm wasm` on starter ROMs and verify the resulting `dist/game.html`.
3. **Verify Client-Side Telemetry HUD & SRAM**:
   - Assert <kbd>F2</kbd> HUD display and localStorage persistence functionality.
