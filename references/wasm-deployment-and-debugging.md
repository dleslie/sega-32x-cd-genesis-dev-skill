# WebAssembly Standalone Deployment & Emulator Debugger Reference

This reference documents the architecture, compilation pipeline, client-side persistence, and real-time hardware debugger/profiler tools for deploying Sega Genesis, Sega CD, and Sega 32X games as **standalone, zero-dependency, single-file HTML WebAssembly applications**.

---

## 1. Single-File Standalone Distribution Model

### Zero Server Dependencies
Traditional web deployments rely on web servers, Nginx/Apache daemons, HTTP range requests, or multi-file asset hosting. For playtesting, archiving, and distribution, this introduces severe friction and hosting dependencies.

The Sega WebAssembly deployment model compiles the entire stack into a **single self-contained `.html` file**:
- **HTML5 shell & UI layout** (retro CRT styling, responsive canvas, virtual gamepad).
- **CSS styling** for desktop, mobile touch displays, and fullscreen presentation.
- **JavaScript emulator glue runtime** (Emscripten modular runtime).
- **Virtual in-memory filesystem (MEMFS)** containing embedded ROM/disc assets.
- **Base64-encoded WebAssembly binary** embedded directly into the HTML script body via Emscripten `-s SINGLE_FILE=1`.

### Instant Portability
The resulting file (e.g., `dist/game.html`) can be:
- Shared via simple file copy, USB flash drive, Discord, email, or cloud storage.
- Opened directly in any modern web browser via `file:///path/to/game.html` with full functionality.
- Hosted on any static web server or CDN (GitHub Pages, itch.io, Netlify) without backend configuration.
- Played completely offline with no network connection required.

---

## 2. Compilation Pipeline & Toolchain

The build pipeline leverages **Emscripten (`emcc`)** compiling a Libretro emulation core (such as **PicoDrive** for 32X/Genesis/CD or **Genesis Plus GX** for Genesis/CD) paired with a specialized C WebAssembly frontend bridge (`wasm_main.c`).

### Compilation Flags Breakdown
```bash
emcc -O2 -flto \
    -Iopt/picodrive \
    -Iopt/picodrive/pico \
    -Iopt/picodrive/cpu/sh2 \
    -Iopt/picodrive/platform/libretro/libretro-common/include \
    src/platform/wasm/wasm_main.c \
    opt/picodrive/libpicodrive_wasm.a \
    -s SINGLE_FILE=1 \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s INITIAL_MEMORY=33554432 \
    -s FORCE_FILESYSTEM=1 \
    -s "EXPORTED_FUNCTIONS=['_main','_malloc','_free','_wasm_set_pad_mask','_wasm_set_button','_wasm_pull_audio_f32','_wasm_set_paused','_wasm_reset_cartridge','_wasm_save_quick_state','_wasm_load_quick_state','_wasm_sync_sram_now','_wasm_get_telemetry_ptr']" \
    -s "EXPORTED_RUNTIME_METHODS=['FS','UTF8ToString','HEAPU8','HEAPU32','HEAPF32']" \
    --embed-file rom/game.32x@/rom/game.32x \
    --shell-file assets/wasm/wasm_shell.html \
    -o dist/game.html
```

- `-s SINGLE_FILE=1`: Inlines the compiled WebAssembly binary as a base64 Data URI inside the output HTML file. Eliminates external `.wasm` download requests.
- `-s ALLOW_MEMORY_GROWTH=1`: Permits the WebAssembly heap to expand dynamically if large save states or high-rate audio buffers require temporary allocation.
- `-s INITIAL_MEMORY=33554432`: Allocates 32 MiB upfront to house the Libretro core state, ROM buffer, framebuffer caches, and audio ring buffers without early reallocations.
- `--embed-file <host_path>@<vfs_path>`: Packs the target cartridge ROM (`.bin`, `.32x`) or CD image directly into the Emscripten virtual filesystem at startup.
- `--shell-file <path>`: Specifies the custom HTML template wrapping the WebAssembly runtime.

---

## 3. Client-Side Persistence Layer (`localStorage`)

Because standalone single-file HTML runs in the browser without a server backend, persistent storage (battery-backed cartridge SRAM and quick savestates) is handled entirely client-side using the browser's `window.localStorage` API.

### UTF-8 Hex Serialization
Browser `localStorage` only stores UTF-16/UTF-8 strings. Raw binary SRAM or savestate dumps containing null bytes (`0x00`) or non-UTF-8 sequences will suffer truncation or corruption.

The persistence bridge resolves this using an efficient two-pass ASCII hexadecimal encoder/decoder:
1. When SRAM changes or state is saved, binary buffers are converted to hexadecimal strings (`[0-9a-f]`) in the virtual filesystem (`/saves/game.srm.hex`, `/saves/quick.state.hex`).
2. An `EM_JS` bridge reads the hex strings and writes them to `localStorage` under a scoped namespace key:
   ```javascript
   localStorage.setItem("<project-slug>:/saves/game.srm.hex", hexData);
   ```
3. On startup, the bridge checks `localStorage`, recreates directory trees in the virtual filesystem, and restores the hex data before the emulator core boots.

### FNV-1a Dirty SRAM Hashing
To prevent thrashing `localStorage` with constant disk writes every frame, `wasm_main.c` computes a 32-bit FNV-1a hash over the active SRAM memory buffer every 120 frames (2 seconds). Serialization to `localStorage` only triggers when the content hash actually changes.

### JSON Save Import & Export
Users can export all saved games and states as a standalone `.json` file (`<project>-saves.json`) and import them on any other browser or machine, guaranteeing that saves are never trapped in a single browser profile.

---

## 4. Fixed 60 Hz NTSC Frame Pacing Loop

Web browsers drive rendering loops using `requestAnimationFrame`, which synchronizes to the display's native refresh rate (e.g., 60 Hz, 120 Hz, 144 Hz, 240 Hz). Running an unthrottled emulator step inside `requestAnimationFrame` on a 144 Hz or 240 Hz monitor causes the game to run at 2.4x–4x hyper-speed with severe audio underruns.

### Delta Accumulator Algorithm
`wasm_main.c` implements a fixed-timestep accumulator in the HTML5 animation frame callback:
```c
static EM_BOOL on_animation_frame(double time, void *user_data)
{
    double delta_ms = time - g_last_frame_time;
    g_last_frame_time = time;

    /* Clamp maximum frame delta to prevent spiral-of-death after tab switching */
    if (delta_ms > 100.0) delta_ms = 100.0;
    g_time_accumulator_ms += delta_ms;

    /* Target exact NTSC 60.0 Hz frame duration: 16.6667 ms */
    const double frame_step_ms = 1000.0 / 60.0;
    while (g_time_accumulator_ms >= frame_step_ms) {
        retro_run();
        g_time_accumulator_ms -= frame_step_ms;
    }
    return EM_TRUE;
}
```

---

## 5. Live Profiler & Hardware Debugger Inspector

The standalone HTML shell incorporates a comprehensive, non-intrusive **Hardware & Emulator Telemetry Inspector** toggled via the **F2** key, backtick (**`**), or the **Debug Info** toolbar button.

### 36-Word Telemetry Protocol
The C WebAssembly frontend exposes a 36-word array (`uint32_t *wasm_get_telemetry_ptr(void)`) accessed directly by JavaScript via `Module.HEAPU32` without memory copying or JSON serialization:

| Index | Field | Description | Architectural Use |
|:---|:---|:---|:---|
| `[0]` | `wasmHeapTotal` | Total WebAssembly runtime memory (bytes) | Detects runtime memory leaks or unbounded growth |
| `[1]` | `wasmHeapUsed` | Active `sbrk(0)` memory watermark (bytes) | Verifies heap stability during gameplay |
| `[2]` | `sdramSize` | 32X SDRAM capacity (`0x40000` = 256 KiB) | Confirms 32X SDRAM allocation |
| `[3]` | `dramSize` | 32X Framebuffer capacity (`0x40000` = 2×128 KiB) | Confirms dual framebuffer mapping |
| `[4]` | `pendingFb` | Active 32X Framebuffer flip status | Detects VDP frame flip stall / VBlank desync |
| `[5]` | `m68kRamSize` | Genesis Work RAM capacity (`0x10000` = 64 KiB) | Confirms 68K main memory mapping |
| `[6]` | `vramSize` | Genesis VDP Video RAM (`0x10000` = 64 KiB) | Confirms VDP tile/sprite memory budget |
| `[7]` | `zramSize` | Z80 Audio Coprocessor RAM (`0x2000` = 8 KiB) | Verifies Z80 sound driver memory limits |
| `[8]` | `sramSize` | Cartridge Battery SRAM capacity (bytes) | Confirms active battery save mapping |
| `[9]` | `sramHash` | 32-bit FNV-1a hash of active SRAM data | Indicates when save file has pending writes |
| `[10]` | `romSize` | Cartridge ROM total size in bytes | Verifies SSF mapper banking and total ROM capacity |
| `[11]` | `msh2Pc` | Master SH-2 Program Counter (`PC`) | Pinpoints exact crash/lockup instruction address |
| `[12]` | `msh2State` | Master SH-2 state (`RUN`, `SLEEP`, `CPOLL`, `VPOLL`) | Diagnoses CPU halt or polling deadlocks |
| `[13..14]` | `msh2Poll` | Master SH-2 polling cycle and count accumulators | Measures Master CPU idle spinning |
| `[15]` | `ssh2Pc` | Slave SH-2 Program Counter (`PC`) | Identifies Slave math/audio crash locations |
| `[16]` | `ssh2State` | Slave SH-2 state (`RUN`, `SLEEP`, `CPOLL`, etc.) | Detects Slave coprocessor deadlocks |
| `[17..18]` | `ssh2Poll` | Slave SH-2 polling cycle and count accumulators | Measures Slave CPU idle wait state |
| `[19]` | `vdpZ80` | High 16: Z80 run flag; Low 16: current VDP scanline | Monitors raster timing and Z80 sound crashes |
| `[20..27]` | `comm0..comm14`| 32X Inter-CPU Communication Registers | Live hex display of inter-CPU handshakes and joypads |
| `[28]` | `coreTimeUs` | Rolling average core emulation frame time ($\mu\text{s}$) | Quantifies frame headroom (< 16,667 $\mu\text{s}$ = 60 FPS) |
| `[29]` | `audioQueued` | Audio ring buffer fill level (stereo frames) | Detects audio buffer starvation or overflow |
| `[30]` | `audioCap` | Audio ring buffer capacity (8192 frames) | Reference for buffer queue headroom |
| `[31]` | `totalFrames` | Total frames stepped since boot | Confirms emulator loop advancement |
| `[32]` | `vdpMode` | 32X VDP Control Register (display mode & flags) | Direct color (16bpp) vs indexed (8bpp) status |

---

## 6. Debugger Telemetry Panels & Diagnostics

### 1. Memory Subsystems Card
Exposes real-time memory usage across both host WebAssembly and emulated console hardware:
- **Wasm Virtual Heap Bar**: Visual gauge showing active memory utilization versus allocated capacity.
- **Cartridge ROM & SRAM**: Shows mapped ROM size (e.g. 8.00 MB SSF Banked) and dirty hash status.
- **Console RAM Subsystems**: Validates mapping of 32X SDRAM (256 KB), Framebuffers (256 KB), M68K RAM (64 KB), VDP VRAM (64 KB), and Z80 RAM (8 KB).

### 2. Multi-CPU Core Activity Card
Provides immediate insight into multi-processor execution states:
- **Master SH-2**: Live Program Counter (`PC`) and execution state badge (`RUN`, `SLEEP`, `POLL COMM`, `POLL VDP`, `POLL SDRAM`).
- **Slave SH-2**: Live Program Counter (`PC`) and coprocessor state badge.
- **Motorola 68000**: Poll cycle counters and execution status.
- **Z80 & YM2612**: Active indicator confirming sound driver execution.

### 3. Inter-CPU COMM Bus Registers Card
Displays an 8-cell live hexadecimal grid of `COMM0` through `COMM14`:
- `COMM0`: Boot handshake and battery SRAM save/load coordination.
- `COMM2`: Frame-exact simulation tick synchronizer.
- `COMM4`: PWM sound effect triggers and Slave ROM pause flags.
- `COMM8..COMM14`: Joypad controller bitmasks for Player 1 and Player 2.

### 4. Frame Pacing & Audio Health Card
- **Core Time per Frame**: Displays the exact microsecond execution time spent simulating each frame against the 16.67 ms (60 FPS) budget.
- **Audio Ring Buffer Bar**: Visual gauge of buffered PCM audio samples. Prevents audio pops, crackles, or stutter by monitoring queue health.
- **Real-Time FPS Badge**: Live frame-rate measurement updated every second.

---

## 7. Controls & Input Mapping

The standalone shell supports three concurrent input modalities:

1. **Desktop Keyboard (Player 1 & Player 2)**:
   - D-Pad: Arrow Keys or `W` `A` `S` `D` (P1) / Numpad 8-4-6-2 (P2)
   - Action Buttons: `Z`/`J` (A), `X`/`K` (B), `C`/`L` (C)
   - 6-Button Aux: `Q` (X), `E` (Y), `R` (Z)
   - System: `Enter` (Start), `Tab`/`Backspace` (Mode)
   - Function Keys: `F2`/`~` (Toggle Debug Inspector), `F5` (Quick Save State), `F8` (Quick Load State)

2. **HTML5 Standard Gamepad API**:
   - Automatically polls connected USB or Bluetooth gamepads (Xbox, PlayStation, 8BitDo, generic HID).
   - Standard mapping maps D-Pad, face buttons (South $\to$ A, East $\to$ B, West $\to$ C, North $\to$ Y), shoulder buttons (LB $\to$ X, RB $\to$ Z), Start, and Select/Mode.

3. **On-Screen Responsive Touch Controls**:
   - Ergonomic 8-way directional D-pad, Start, Mode, and 6 action buttons (A, B, C, X, Y, Z).
   - Touch buttons feature active color styling and pointer event isolation for smooth multi-touch play on mobile devices and tablets.
