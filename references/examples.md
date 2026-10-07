# Landmark Examples, Project Index, & SGDK 1.x → 2.x Migration Reference

This document indexes landmark open-source projects, genre-specific production ports, technical references across the Sega Genesis, Sega CD, and 32X ecosystems, and provides a comprehensive **SGDK 1.x → 2.x API migration table**.

---

## 1. Landmark Open-Source Reference Projects

### A. Sega 32X Production Implementations
1. **DOOM 32X Resurrection (d32xr)** (`viciious/d32xr` - *architectural reference only; Jaguar DOOM source code explicitly excluded*):
   - Dual-SH2 pipelined rendering: Master SH-2 processes BSP traversal, player simulation, and column drawing; Slave SH-2 processes plane fills, span texture mapping, and sprite rasterization.
   - Hand-crafted SH-2 assembly column and span drawers with reciprocal division LUTs.
   - Cache-through peripheral access and DMA buffer alignment (`ATTR_DATA_CACHE_ALIGN`).
2. **Virtua Racing 32X Disassembly** (`matiaszanolli/sega-vr-disasm`):
   - Decoupled 30 Hz / 60 Hz physics simulation from rendering frames.
   - Swap-only V-INT handlers: Interrupt routines only acknowledge flips and update hardware scroll registers, eliminating blocking inter-CPU waits during VBlank.
3. **Sonic the Hedgehog 1 Disassembly** (`sonicretro/s1disasm`):
   - Pattern Load Cues (PLC): Buffered DMA queues spreading tile uploads across VBlanks to prevent VRAM bus starvation.
   - Dual-layer 16×16 tile chunk collision arrays with precomputed height and angle tables.
4. **PWM Tracker 32X** (`haroldo-ok/tracker-player-32x`):
   - Full 8-voice software tracker mixing 8 independent virtual voices down to stereo PWM FIFOs @ 11,025 Hz on one SH-2.
   - Demonstrates priority SFX voice stealing and sample rate cycle calculation.
5. **Zepton 32X** (`haroldo-ok/zepton-32x`):
   - 3D voxel landscape and billboard sprite rendering on 32X.
   - Measured fillrate vs arithmetic bottlenecks and column length optimization.
6. **Arkanoid 32X** (`haroldo-ok/arkanoid-32x`):
   - 7 fps $\to$ 60 fps optimization case study via dirty-rectangle compositing with cached backgrounds and polygon slope precomputation.
7. **Wave Rider GP 32X**:
   - 3D water racer running streaming 4-bit IMA-ADPCM soundtrack + SFX on Slave SH-2 concurrent with Master 3D rendering.

---

### B. Sega Genesis / SGDK Production Ports (Haroldo-OK Collection)
1. **God of Thunder Genesis** (`haroldo-ok/god-of-thunder-genesis`):
   - The canonical reference for porting DOS action-adventure titles.
   - Demonstrates Mode X to VDP tilemap conversion, 4×16 palette quantization, and sound effect translation.
2. **Jazz Jackrabbit for Sega Genesis** (`haroldo-ok/jazz-jackrabbit-for-sega-genesis`):
   - High-speed platformer with complete automated two-tier testing (host C logic oracle + headless Genesis Plus GX frame assertions).
3. **convert-s3m-vgm-jazzjackrabbit-md**:
   - Production pipeline for converting tracker modules (`.s3m`) into authentic YM2612 + SN76489 VGM logs compiled for XGM2.
4. **F-Zero Clone MD** (`haroldo-ok/fzero-clone-md`):
   - Mode-7 affine road streaming on Genesis VDP using per-scanline H-scroll tables and dynamic tile uploads.
5. **Text Elite Genesis** (`haroldo-ok/text-elite-genesis`):
   - Pure C engine port with full procedural galaxy generation running in 68 KiB RAM.

---

## 2. SGDK 1.x → 2.x API Migration Table

When porting or studying older SGDK codebases (SGDK 1.6x–1.9x), use this table to update deprecated functions to modern SGDK 2.x conventions:

| Deprecated SGDK 1.x Symbol | Modern SGDK 2.x Replacement | Notes & Architectural Rationale |
| :--- | :--- | :--- |
| `VDP_waitVInt()` | `SYS_doVBlankProcess()` | **Critical:** `SYS_doVBlankProcess()` flushes DMA queues, updates sprites, and waits for VBlank. Calling old wait stalls modern engines. |
| `VDP_setPalette(num, data)` | `PAL_setPalette(num, data, DMA)` | Explicit transfer method (`DMA` or `CPU`) is now required. |
| `VDP_setPaletteColor(idx, col)` | `PAL_setColor(idx, col)` | Unified under `PAL_*` namespace. |
| `VDP_fadeIn(...)` / `VDP_fadeOut(...)` | `PAL_fadeIn(...)` / `PAL_fadeOut(...)` | Palette fading moved to `pal.h`. |
| `XGM_startPlay(song)` | `XGM2_play(song)` | Modern XGM2 driver replaces legacy XGM driver. |
| `XGM_stopPlay()` | `XGM2_stop()` | Simplified transport naming. |
| `XGM_pausePlay()` | `XGM2_pause()` | Simplified transport naming. |
| `XGM_resumePlay()` | `XGM2_resume()` | Simplified transport naming. |
| `XGM_setLoopNumber(n)` | `XGM2_setLoopNumber(n)` | `-1` loops infinitely. |
| `XGM_startPlayPCM(id, prio, ch)` | `XGM2_playPCM(data, size, ch)` | XGM2 takes sample data pointer and size directly. |
| `JOY_waitPressBtn()` | `JOY_waitPress(port, mask)` | Requires specific port and button bitmask. |
| `JOY_setSupport(port, sup)` | Auto-detected | Hardware controller type is now auto-detected by `JOY_init()`. |
| `BMP_init(...)` / `BMP_flip(...)` | Software Bitmap Mode | Deprecated in SGDK 2.x; prefer modern tilemap or 32X framebuffer. |
| `SPR_init(max, vram, alloc)` | `SPR_init()` | Sprite allocation table sizing is now automated. |
| `SPR_addSprite(def, x, y, attr)` | `SPR_addSprite(...)` | Returns `Sprite*` handle; now requires `SpriteDefinition*`. |
| `VDP_drawBitmap(...)` | `VDP_drawImage(...)` | Consolidated into standard `Image` resource model. |
| `SYS_hardReset()` | `SYS_reset()` | Consolidated reset function. |

---

## 3. Techniques & Genre-Specific Reference Map

| Technical Need | Recommended Reference Codebase | Key Technique / Module to Study |
| :--- | :--- | :--- |
| **Pipelined Dual-Core 3D** | `viciious/d32xr` | `marsnew.c`, `r_phase*.c`, Master/Slave COMM job dispatch. |
| **8-Voice Software Sound Mixer** | `haroldo-ok/tracker-player-32x` | Stereo PWM FIFO feeding, virtual voice channel stealing. |
| **Streaming Compressed Audio** | `wave-rider-gp` / Devilution32X | Slave SH-2 IMA-ADPCM streaming concurrent with SFX. |
| **Automated Emulator Testing** | `haroldo-ok/jazz-jackrabbit-for-sega-genesis` | Libretro headless C harness, PPM pixel assertions. |
| **Terrain / Voxel Rendering** | `haroldo-ok/zepton-32x` | Depth-slice reciprocal projection, billboard column draws. |
| **Dirty-Rectangle 2D** | `haroldo-ok/arkanoid-32x` | Double-buffered dirty lists (`s_dirty[2]`), background restore. |
| **Banked ROM Cartridges (>4 MB)** | Devilution32X | Sega SSF Mapper registers (`0xA130F1..0xA130FF`). |
| **SRAM Save Persistence** | `warcraft-32x` | Two-phase commit, checksum validation, odd-byte interleaving. |
