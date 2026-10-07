# Porting Workflow & Architectural Migration Reference

Porting an existing game (from DOS, PalmOS, arcade machines, C/C++ desktop engines, or retro home computers) to the Sega Genesis, Sega CD, or Sega 32X requires a structured architectural migration. This reference outlines the core/shell design pattern, the subsystem mapping table, memory adaptation, and the step-by-step bringup order.

---

## 1. Core/Shell Architectural Pattern

Never interleave hardware register calls (`VDP_*`, `MARS_*`, `COMM*`) directly into game logic. Maintain a strict separation of concerns:

```
┌────────────────────────────────────────────────────────┐
│               PORTABLE CORE (src/core/)                │
│  - Physics simulation, actor AI, inventory management  │
│  - Combat math, pathfinding, procedural generators     │
│  - Pure C11 / C99: Zero hardware headers, zero floats  │
│  - Compiles on both local host (oracle) and console    │
└───────────────────────────┬────────────────────────────┘
                            │
                            ▼
┌────────────────────────────────────────────────────────┐
│            PLATFORM SHELL (src/platform/)              │
│  - Genesis / 32X / Sega CD HAL and hardware wrappers   │
│  - Framebuffer flipping, VDP tilemap/sprite upload     │
│  - Pad polling, audio streaming, save persistence      │
└────────────────────────────────────────────────────────┘
```

---

## 2. Universal Subsystem Mapping Table

| Original PC / DOS Subsystem | Sega Genesis (SGDK) Equivalent | Sega 32X (Mars) Equivalent | Sega CD Equivalent |
| :--- | :--- | :--- | :--- |
| **Display Mode**<br>VGA Mode 13h (320×200, 256 colors) | VDP Tilemaps (`BG_A`, `BG_B`) + 80 Hardware Sprites (H40: 320×224). | 32X Packed-Pixel Framebuffer (320×224, 256 indexed colors). | ASIC Affine Transformation Canvas into Word RAM. |
| **Palette Model**<br>256 colors from 18-bit VGA DAC (0–63 per channel) | 4 palettes × 16 colors (9-bit BGR, 0–7 per channel). Index 0 is transparent. | 256 CRAM colors (15-bit RGB555). Index 0 is transparent. | 64 Genesis colors or ASIC 8bpp color lookups. |
| **System Timing**<br>18.2 Hz DOS timer / 70 Hz VGA refresh | 60 Hz NTSC / 50 Hz PAL VBlank interrupt (`SYS_doVBlankProcess`). | 60 Hz NTSC / 50 Hz PAL VBlank counter (`Mars_FlipFrameBuffers`). | 60 Hz VBlank interrupt with Sub-CPU timer ticks. |
| **Main RAM**<br>640 KiB DOS Conventional Memory | **68 KiB** Work RAM (`0xFF0000`). Strict static layout; no heap. | **256 KiB** SDRAM (`0x06000000`). Shared between both SH-2s. | **512 KiB** PRG-RAM + **256 KiB** Word RAM. |
| **File I/O**<br>`fopen()`, `fread()`, dynamic disk files | Static Cartridge ROM (<4 MiB) or Banked Cartridge ROM (<32 MiB). | Banked Cartridge ROM (Sega SSF Mapper, up to 32 MiB). | **ISO-9660 CD-ROM Streaming** (150 KB/s transfer rate). |
| **Audio**<br>Sound Blaster PCM + AdLib OPL FM / MIDI | Yamaha YM2612 (6 FM) + TI SN76489 (4 PSG) + 4-ch XGM2 PCM. | Stereo PWM Unit (~11–22 kHz) + Genesis FM/PSG co-processor. | CD-DA Redbook Audio + Ricoh RF5C164 8-ch PCM. |
| **Controller**<br>PC Keyboard & Mouse | 3-Button / 6-Button Gamepad (`JOY_readJoypad`). | 3-Button / 6-Button Gamepad + Sega Mouse (`Mars_PollMouse`). | Standard Sega Gamepad + Mega Mouse. |
| **Save Data**<br>`save.dat` files on disk | Battery-Backed SRAM (`0x200000`, odd byte addresses). | Battery-Backed SRAM via 68000 bus window. | 16 KiB Internal Backup RAM (BRAM) or Backup Cartridge. |

---

## 3. Memory Architecture & Constraint Adaptations

### A. Fitting Large PC Memory into Console RAM
1. **Move Immutable Data to ROM:** PC games frequently allocate MBs of RAM to hold loaded sprite graphics, sound banks, and level maps. On console, leave all read-only assets in Cartridge ROM or stream directly from CD.
2. **Object Pooling:** Replace dynamic `malloc()` / `free()` loops with static pre-allocated arrays (e.g. `D32Actor actors[MAX_ACTORS]`).
3. **Sparse State Diffs:** For large mutable worlds, persist only depleted entities or altered coordinates rather than duplicating entire maps.

### B. Decoupling Game Logic Ticks from 60 Hz Display Refresh
Many retro PC games tick at ~18.2 Hz (DOS timer) or 20 Hz, whereas consoles refresh at 60 Hz (NTSC) or 50 Hz (PAL):
- **Accumulator Loop Pattern:**
  ```c
  static int32_t tick_accumulator = 0;
  // Advance simulation by source rate (e.g. 20 Hz = 3 display frames per logic tick)
  tick_accumulator += 1;
  while (tick_accumulator >= 3) {
      game_logic_tick();
      tick_accumulator -= 3;
  }
  render_frame();
  ```

---

## 4. Incremental Bringup Order (The 7-Step Method)

Never attempt to bring up graphics, input, sound, and gameplay simultaneously. Follow this strict step-by-step ladder:

```
Step 1: Host Logic Oracle   ──► Build portable core on desktop, verify game state
            │
Step 2: Headless Test Setup ──► Hook synthetic tests, verify simulated player/combat
            │
Step 3: Video Init Stub     ──► Boot on console, clear screen, display lit color/tile
            │
Step 4: Real Renderer       ──► Port tilemap and sprite rasterizer; verify no black screen
            │
Step 5: Joypad Input        ──► Map controller buttons; verify movement across states
            │
Step 6: Audio Playback      ──► Wire VGM chiptune or PWM sound effects
            │
Step 7: Save Persistence    ──► Wire battery-backed SRAM with two-phase commit
```

1. **Step 1: Build the Portable Core on Host.** Ensure core game rules, math, and data structures compile cleanly under `gcc -Wall -Wextra -Werror` with zero console dependencies.
2. **Step 2: Verify Game State with Oracle Tests.** Write unit tests asserting that mock inputs move actors, execute combat, and decrement hit points.
3. **Step 3: Console Video Stub.** Bring up the minimal console entry point (`main()`), initialize the VDP / 32X VDP, seed a test palette, and display a solid colored rectangle or test text string. Verify in headless emulator.
4. **Step 4: Bring Up Real Rendering.** Wire tile blitting or framebuffer drawing. Confirm that frames are lit, colourful, and updating.
5. **Step 5: Wire Controller Input.** Map `JOY_*` or `Mars_ReadController()` to the core's input structure. Script button presses in automated tests to prove the player can traverse menus.
6. **Step 6: Wire Audio.** Bring up music and sound effects. Audit bus bandwidth to ensure sound playback does not starve the rendering loop or cause FIFO buzz.
7. **Step 7: Wire Save Persistence.** Implement SRAM reading and writing with two-phase commit and validation checks.
