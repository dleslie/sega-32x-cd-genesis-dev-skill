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

## Phase 3: Continuous Integration (GitHub Actions Matrix) (Completed)

### Goal
Establish an automated CI workflow that tests every push across all supported target platforms and verification tiers.

### Work Items
1. **Create `.github/workflows/ci.yml`**:
   - Runner: `ubuntu-latest`.
   - Steps:
     - Check out repo with Git LFS (`actions/checkout@v4` with `lfs: true`).
     - Run `bash assets/setup.sh` to extract toolchains into `opt/`.
     - Compile Tower of Power stack (`make -f assets/Makefile.tower`).
     - Compile Sega CD turnkey disc image (`make -f assets/Makefile.cd`).
     - Run `bash assets/tests/run_all.sh` asserting all host logic and headless emulator smoke tests pass.
     - Upload build and test artifacts (`actions/upload-artifact@v4`).

---

## Phase 4: Standalone WebAssembly Runtime & Verification (Completed)

### Goal
Make single-file HTML WebAssembly export completely turnkey and verify telemetry and persistence capabilities.

### Work Items
1. **Bundle WebAssembly Runtime Library**:
   - Compiled PicoDrive with Emscripten into static library with complete VFS/stream support.
   - Packaged prebuilt `libpicodrive_wasm.a` and all C headers into `artifacts/picodrive-wasm.tar.gz`.
   - Updated `assets/setup.sh` to extract the Wasm runtime automatically.
2. **Test Single-File HTML Generation**:
   - Updated `assets/wasm/Makefile.wasm` with direct Emscripten path resolution.
   - Built `dist/tower_game.html` embedding 32X Tower ROM, virtual gamepad, audio ring buffers, and CRT canvas.
   - Deployed standalone application to `/var/www/apps/tower_game/index.html` and registered on WebApps Hub.
3. **Verify Client-Side Telemetry HUD & SRAM**:
   - Verified 32-bit FNV-1a dirty SRAM hashing and bidirectional `localStorage` persistence.
   - Verified real-time telemetry inspector (<kbd>F2</kbd>) exposing dual SH-2 program counters, 32X COMM registers, and emulation performance metrics.
   - Verified LAN endpoints (127.0.0.1, 192.168.1.3, 192.168.192.4) with HTTP 200 responses.

---

## Phase 5: Visual Detailing, Sega CD Audio Architecture, & Particle FX (Planned)

### Goal
Enhance visual fidelity and audio-visual immersion of the 32X starfighter simulation by adding polygon wireframe accents, leveraging the Sega CD Sub-68000 for dynamic audio playback, and simulating thruster particle exhausts.

### Work Items
1. **Polygon Outlines & Cockpit Detailing**:
   - Render cockpit canopy edge outlines and subtle wireframe struts over cyan glass panels to sharpen visual definition against high-contrast starfield backgrounds.
   - Implement optional silhouette and edge accenting for outer wing tips and rudder fins.
2. **Sub-68000 CD-DA & Ricoh PCM Audio Engine**:
   - Leverage the Sega CD Sub-68000 coprocessor to drive dynamic audio responding to Genesis Main-68000 flight telemetry.
   - Implement Ricoh RF5C164 8-channel PCM engine hum loops with pitch modulation tied to ship velocity and thruster state.
   - Integrate Redbook CD-DA background music playback orchestration via the Sub-CPU BIOS interface.
3. **Thruster Particle System & Space Dust**:
   - Implement an active 3D particle emitter behind the engine exhaust nozzle (vertex 15).
   - Generate velocity-scaled exhaust plume particles with depth decay and fade-out.
   - Expand space dust density and parallax response to user-driven yaw, pitch, and roll maneuvers.

