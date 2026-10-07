# Antipatterns & "Don't Do This" in Sega Genesis, Sega CD, and 32X Development

This document catalogs anti-patterns, common architectural pitfalls, false assumptions, and explicit "Don't Do This" rules discovered across hardware documentation, compiler toolchains, landmark disassemblies, and production ports (DOOM 32X Resurrection, Virtua Racing 32X, Sonic 1, SGDK, Haroldo-OK ports, and Devilution32X).

---

## 1. Toolchain, Build, & Compiler Antipatterns

### ❌ Don't Link Mismatched Prebuilt `libmd.a` (The LTO Trap)
- **Symptom:** The ROM compiles and links cleanly, boots, and draws background tiles, but **sprites never move and game state appears completely frozen**; writes to RAM globals silently fail to persist while direct VDP register writes work.
- **Cause:** SGDK's prebuilt `libmd.a` carries Fat-LTO bytecode tied to the exact GCC release that built it. Linking against a slightly different GCC version (or attempting to bypass linker version errors with `-fno-lto`) extracts incompatible object code where `.data` initialization copy routines and DMA queue flush loops silently break.
- **Rule:** **Never link a prebuilt `libmd.a` with a different compiler version.** Always rebuild `libmd.a` from source using your exact host/cross compiler (`make -f makelib.gen release`).

### ❌ Don't Use Debian/Ubuntu's Default `-lgcc` on Motorola 68000
- **Symptom:** Game crashes with an Illegal Instruction (Line-F / Line-A) or reset loop when performing 32-bit division or multiplication.
- **Cause:** Debian/Ubuntu's `gcc-m68k-linux-gnu` builds `libgcc.a` targeting the 68020+ architecture by default. Helpers like `__divsi3` use 32-bit branch instructions (`bsr.l`) that do not exist on the base 68000.
- **Rule:** Explicitly link SGDK's 68000-safe `$GDK/lib/libgcc.a` *after* `libmd.a` on the link line instead of `-lgcc`.

### ❌ Don't Emit GNU Build IDs into Genesis Cartridge ROMs
- **Symptom:** Linker errors: `section .note.gnu.build-id LMA [0x00000000...] overlaps section .text`.
- **Cause:** Modern GCC/binutils inserts an ELF `.note.gnu.build-id` at address 0, colliding with the 68000 exception vector table.
- **Rule:** Always pass `-Wl,--build-id=none` when linking 68000 Genesis ROM binaries.

### ❌ Don't Compile Timing-Critical Audio/DMA Handlers with Global `-flto`
- **Symptom:** Audio sample playback buzzes, crackles, or drops channels unpredictably across release builds.
- **Cause:** Whole-program Link-Time Optimization (`-flto`) reorders memory accesses, inlines delay loops, and alters cycle timing in critical FIFO-filling routines and polling handshakes.
- **Rule:** Isolate PWM mixers, Z80 driver feeds, and hardware handshake files into separate translation units compiled with `-O2 -fno-lto`.

---

## 2. 68000 & VDP (Genesis) Antipatterns

### ❌ Don't Perform Narrowing Casts from Odd Memory Addresses
- **Symptom:** Address Error exception (red crash screen or emulator dump) on real hardware and cycle-accurate emulators, but runs fine in lax emulators.
- **Cause:** Extracting the integer portion of a custom fixed-point 32-bit struct field via `(s16)(val >> 8)` causes GCC 14+ to emit a 16-bit word load at an odd byte boundary (`offset + 1`). The 68000 strictly forbids word or long accesses to odd addresses.
- **Rule:** Always force values through a 32-bit data register before narrowing:
  ```c
  static inline int16_t fromfp(int32_t v) { return (int16_t)(v >> 8); }
  ```

### ❌ Don't Loop `VDP_setTileMapXY()` in Real-Time Loops
- **Symptom:** Severe framerate drops, dropped inputs, and tearing.
- **Cause:** Calling `VDP_setTileMapXY()` sends per-cell VDP commands over the 68000 bus, stalling the CPU on bus wait-states and blowing the vertical blanking window.
- **Rule:** Build tile attribute rows or rectangles into a contiguous RAM buffer and queue them using DMA (`VDP_setTileMapDataRect()` or `DMA_queue()`), or stream large worlds using SGDK's `MAP_*` engine.

### ❌ Don't Flood the DMA Queue During Active Display
- **Symptom:** Visual corruption, screen tearing, and severe audio stuttering/dropouts.
- **Cause:** The Genesis bus grants DMA highest priority, locking the 68000 out of memory and starving the Z80 audio driver from accessing the ROM/bus to feed the YM2612. Active-display DMA bandwidth is limited to ~7.6 KB per frame.
- **Rule:** Restrict heavy DMA transfers to VBlank (max ~18 KB in NTSC). Split full-plane tilemap updates or palette reloads across multiple frames.

### ❌ Don't Treat Palette Color Index 0 as an Opaque Color
- **Symptom:** Background artwork shows through foreground sprites or tile planes unexpectedly.
- **Cause:** In Genesis VDP hardware, color index 0 of every palette row (PAL0..PAL3) is hardwired as transparent for sprites and tile planes.
- **Rule:** Reserve color index 0 strictly for transparency. Place solid background colors in indices 1..15.

### ❌ Don't Hand-Scale VGA 6-Bit DAC Colors by ×4
- **Symptom:** Blown-out, saturated, or distorted colors when porting DOS/PC graphics.
- **Cause:** Genesis uses 9-bit BGR (3 bits per RGB channel, values 0..7). Multiplying a 6-bit DAC value (0..63) by 4 treats it as 8-bit (0..255) and truncates the top bits incorrectly.
- **Rule:** Quantize colors properly: `vdp_channel = (vga_channel >> 3) & 0x7`. Let `rescomp` handle palette quantization when possible.

---

## 3. Sega 32X (Hitachi Dual SH-2 & Mars VDP) Antipatterns

### ❌ Don't Clear and Redraw the Entire Framebuffer Every Frame
- **Symptom:** Framerate collapses to 15–20 fps, and PWM audio emits a loud 60 Hz frame-buzz.
- **Cause:** The 32X framebuffer at `0x24000000` is uncached I/O memory. Writing 320×224 bytes (~72 KB) consumes immense bus bandwidth and locks out PWM FIFO refills.
- **Rule:**
  1. Only redraw dirty regions or moving entities.
  2. Maintain separate dirty rectangle lists per framebuffer: `s_dirty[2]` (because the 32X uses page flipping; a rectangle dirtied on frame $N$ remains dirty in the buffer displayed on frame $N+2$).
  3. Pre-render static HUDs and backdrops once into an offscreen SDRAM buffer and restore dirty rectangles.

### ❌ Don't Rely on Implicit Cache Coherency Between SH-2 Cores
- **Symptom:** Game works in emulators (like PicoDrive with dynarec) but locks up or displays corrupted geometry on real 32X hardware.
- **Cause:** Each SH-2 core has a private 4 KiB on-chip direct-mapped cache. SDRAM at `0x06000000` is cached. If Master SH-2 writes a command packet or mesh data to SDRAM, Slave SH-2 will read stale cache lines unless the memory is synchronized.
- **Rule:**
  - Route shared command blocks through **cache-through address space** (`0x26000000` for SDRAM, `0x20000000` for Mars registers / ROM).
  - Explicitly purge cache lines using `Mars_ClearCacheLine()` or `Mars_ClearCache()` when sharing data in cached SDRAM.

### ❌ Don't Poll COMM Registers with Unbounded Loops
- **Symptom:** Complete system freeze if one CPU experiences an interrupt or delay.
- **Cause:** `while (MARS_SYS_COMM4 != 0);` blocks indefinitely if the slave crashes or is stalled by audio/DMA.
- **Rule:** Always bound inter-CPU polling loops:
  ```c
  uint32_t timeout = 200000;
  while (MARS_SYS_COMM4 != 0 && --timeout);
  if (timeout == 0) { /* Fallback: recover, log error, or skip phase */ }
  ```

### ❌ Don't Overlap COMM Register Allocations
- **Symptom:** Doubled-image black screens, dropped inputs, or random crashes.
- **Cause:** Using `COMM4` for slave job dispatch while the boot firmware or 68000 uses `COMM4` for the `S_OK` handshake causes race conditions that corrupt Mars registers.
- **Rule:** Maintain a strict, explicit COMM register map:
  - `COMM0` / `COMM2`: Controller input polling & telemetry.
  - `COMM4`: Boot handshake (`M_OK` / `S_OK`).
  - `COMM6`: Slave core heartbeat / liveness counter.
  - `COMM8`..`COMM14`: Application job commands, parameters, and return codes.

### ❌ Don't Assume `long` is 64-Bit on SH-2
- **Symptom:** Distance calculations, hit detection, or AI logic pass on host tests but fail on 32X.
- **Cause:** On SH-2 GCC, `sizeof(int) == 4` and `sizeof(long) == 4`. Distance-squared operations (`dx*dx + dy*dy`) in 16.16 fixed point easily overflow 32 bits.
- **Rule:** Use `int64_t` or `long long` for distance-squared and high-precision intermediate accumulators.

### ❌ Don't Execute Runtime Divides in Hot Pixel/Vertex Loops
- **Symptom:** Frame time spikes dramatically on 3D projections or raycasting.
- **Cause:** The SH-2 lacks a single-cycle hardware divide instruction. 32-bit integer division takes 30–40 cycles, and 64-bit division calls `__divdi3` in libgcc.
- **Rule:**
  - Replace division by powers of 2 with shifts (`>>`).
  - Replace projection division `screen_x = (world_x * focal) / z` with precomputed reciprocal tables:
    ```c
    screen_x = (world_x * focal * recip_table[z >> 12]) >> (RECIP_SH + 12);
    ```

---

## 4. Sega CD / Mega-CD Architecture Antipatterns

### ❌ Don't Access Word RAM Without Verifying Mode & Priority
- **Symptom:** Bus stalls, graphical tearing, or system lockups.
- **Cause:** In 2M mode, the entire 256 KiB Word RAM belongs to either the Main-CPU or Sub-CPU. Accessing Word RAM when the other CPU holds bus priority locks the requesting CPU.
- **Rule:** Always poll the `RET` / `DMNA` control flags in the communication registers before accessing Word RAM. In 1M mode, alternate banks cleanly between display and rendering.

### ❌ Don't Execute Sub-CPU Code from Word RAM During Bank Swapping
- **Symptom:** Sub-CPU crashes into unmapped memory or executes garbage instructions.
- **Cause:** Toggling Word RAM bank ownership while executing code residing in that bank unmaps the running program.
- **Rule:** Run all Sub-CPU drivers, interrupt handlers, and communication loops from **Program RAM (512 KiB)**. Use Word RAM strictly for graphics stamping, framebuffers, and streaming buffers.

### ❌ Don't Stream Uncompressed CD-DA Without Buffering for Seek Latency
- **Symptom:** Game freezes or audio drops out during level transitions or track loops.
- **Cause:** The Sega CD 1× drive has seek times up to 800 ms. Calling BIOS CD-DA play commands mid-game halts or lags audio playback if the laser head must reposition.
- **Rule:** Pre-seek CD tracks during non-interactive loading screens or mask track transitions with Ricoh PCM ambient cues.

---

## 5. Audio (YM2612, PSG, PWM, & XGM) Antipatterns

### ❌ Don't Emit YM2612 Key-Off and Key-On at the Exact Same Timestamp
- **Symptom:** Notes randomly fail to play or go completely silent.
- **Cause:** The YM2612 hardware envelope generator requires a release phase before re-triggering. Emitting key-off and key-on at identical timestamps prevents the envelope generator from seeing the transition.
- **Rule:** Always insert a minimum **1.5 ms gap** (`KEY_GAP_S`) between key-off and key-on in VGM logs or sound drivers.

### ❌ Don't Touch YM2612 Register `0x2B` After Enabling DAC
- **Symptom:** Drum tracks and sampled sound effects vanish completely on real hardware.
- **Cause:** Register `0x2B` enables/disables the DAC on FM Channel 6 (`0x80` = DAC enable, `0x00` = normal FM). Global initialization sequences that overwrite `0x2B` after DAC startup disable the DAC output.
- **Rule:** Reserve FM Channel 6 exclusively for DAC audio when sampled percussion is present. Do not emit register `0x2B = 0x00` writes in track data.

### ❌ Don't Starve the 32X PWM Audio FIFO
- **Symptom:** PWM audio degrades into a buzzing, harsh 60 Hz hum.
- **Cause:** The hardware PWM FIFO holds only a small number of samples (4–8 words). If the CPU rendering thread delays sample pushes past the FIFO drain rate, the FIFO underruns.
- **Rule:**
  - Dedicate the **Slave SH-2** exclusively to audio mixing and FIFO feeding.
  - Mix at a robust, sustainable rate (e.g., 11,025 Hz or 22,050 Hz).
  - Use timer interrupts or DMA streaming rather than unbounded main-loop polling.

### ❌ Don't Route Bass Frequencies to the SN76489 PSG
- **Symptom:** Bass notes play out of tune or produce gross ~890-cent pitch errors.
- **Cause:** The PSG tone generator frequency divider bottoms out around ~C2 (~65 Hz). It cannot produce notes below this frequency without pitch clamping.
- **Rule:** Always route bass and sub-bass parts to YM2612 FM channels. Only route high melody, arpeggios, and noise effects to PSG.

---

## 6. Testing & Emulation Antipatterns

### ❌ Don't Assume "Compiles Cleanly" Means "Boots"
- **Symptom:** A clean build produces `rom.bin`, but running it yields an immediate black screen.
- **Cause:** Missing palette initialization, unhandled VBlank interrupts, infinite wait loops, or stripped entry points (`--gc-sections`).
- **Rule:** Always enforce a two-tier verification gate:
  1. Tier 1: Host logic oracle (fast logic assertion).
  2. Tier 2: Headless emulator (Genesis Plus GX / PicoDrive) asserting non-black, colourful, changing frames.

### ❌ Don't Inject Controller Inputs on Frame 0
- **Symptom:** Automated tests report menu transitions or start buttons failing to work.
- **Cause:** Real hardware and accurate emulators require 20–30 frames to complete power-on reset, initialize VDP registers, and clear memory before accepting gamepad inputs.
- **Rule:** Always run an initial idle period (`run 30`) before injecting scripted joypad inputs.

### ❌ Don't Equate Emulated Frames to Game Logic Iterations
- **Symptom:** Automated tests capture blank screens because enemies or events have not spawned.
- **Cause:** Under heavy rendering workloads, a game running at 20 fps advances its simulation once every 3 emulated frames. Scripting `run 60` advances the game logic by only 20 ticks.
- **Rule:** Calibrate test timeouts against internal simulation ticks or use explicit state markers rather than fixed emulated frame counts.

---

## 7. Fixed-Point & Floating-Point Math Antipatterns

### ❌ Don't Call Software Floating-Point (`float`, `double`, `math.h`) in Frame Loops
- **Symptom:** Unexplained, catastrophic framerate collapse from 60 fps to 10–15 fps upon adding physics, 3D math, or particle routines.
- **Cause:** Neither Motorola 68000, Z80, nor Hitachi SH-2 possess a hardware Floating-Point Unit (FPU). Any floating-point operation emits software emulation library calls (`__addsf3`, `__mulsf3`, `__divsf3`) taking 80 to 5,000 clock cycles per operation.
- **Rule:** Always use **Q16.16 fixed-point math** (`fix16_t` from `assets/fixmath/`) or Q8.8. On SH-2, use the hardware-accelerated 4-cycle `dmuls.l` + `xtrct` multiplier sequence (`fix16_fast_mul_sh2`).

### ❌ Don't Allocate Multi-Kilobyte Trigonometry LUTs in Console Working RAM
- **Symptom:** Stack overflow or `.bss` section collision on Genesis (64 KiB Work RAM) or 32X (256 KiB SDRAM).
- **Cause:** Porting desktop math libraries that allocate large static cache tables (e.g., 4096-entry 32-bit sin/atan caches = 16–32 KiB of RAM), consuming 25–50% of total available console RAM.
- **Rule:** Use zero-RAM polynomial parabolic approximations (`fix16_sin`, `fix16_atan2` in `assets/fixmath/fix16_trig.c`). They consume 0 bytes of RAM and calculate in ~16 CPU cycles with < 0.05% error.

### ❌ Don't Ignore Q16.16 Fixed-Point Overflow in Distance/Dot-Product Calculations
- **Symptom:** Camera jumps, world wrapping, or entities snapping to screen bounds when moving far from the origin.
- **Cause:** In signed Q16.16, the maximum representable integer is $32,767$. Squaring a coordinate distance $> 181$ ($181^2 = 32,761$) overflows a 32-bit integer before the result can be shifted.
- **Rule:** Use saturating math (`fix16_smul`, `fix16_sadd`), pre-scale coordinates before computing vector lengths ($(\Delta x / 2)^2 + (\Delta y / 2)^2$), or compute dot products using 64-bit accumulators.

### ❌ Don't Perform Runtime 32-bit Integer Division in Inner Raster Loops
- **Symptom:** Software 3D polygon rasterizers, perspective projection, or voxel columns crawl at single-digit framerates.
- **Cause:** Neither 68000 nor SH-2 has a single-cycle 32-bit hardware divider (SH-2 requires 32 sequential 1-cycle `div1` step instructions).
- **Rule:** Hoist divisions out of raster loops, replace divisions with multiplication by pre-computed reciprocal lookup tables (`RECIP_SH`), or use binary restoring division.

