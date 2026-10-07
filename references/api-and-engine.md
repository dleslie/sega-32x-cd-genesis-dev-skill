# Combined Runtime API & Hardware Engine Reference (Genesis, 32X, & Sega CD)

This reference provides a complete, unified programming manual for the runtime APIs across **Sega Genesis (SGDK 2.x)**, **Sega 32X (32XDK / Mars)**, and **Sega CD (Mega-CD)**.

---

## 1. Sega Genesis Runtime API (SGDK 2.x)

Headers: `#include <genesis.h>`

### A. Lifecycle & Main Loop Skeleton
```c
#include <genesis.h>

int main(bool hardReset)
{
    // hardReset is TRUE on power-on cold boot, FALSE on console soft reset
    JOY_init();
    SPR_init();                       // Initialize dynamic sprite manager
    PAL_setPalette(PAL0, my_palette.data, DMA);
    VDP_drawText("SYSTEM READY", 10, 12);

    while (TRUE)
    {
        u16 joy = JOY_readJoypad(JOY_1);
        if (joy & BUTTON_START) { /* Handle pause / start */ }

        // Update game simulation / actor physics here
        SPR_update();                 // Update sprite attribute table (SAT)

        SYS_doVBlankProcess();        // MUST be called every frame: flushes DMA & waits VBlank
    }
    return 0;
}
```
`SYS_doVBlankProcess()` flushes the DMA queue, updates scroll registers, advances the sprite engine, and waits for VBlank. Forgetting it in any execution loop freezes the display engine and is the #1 cause of black screens.

Per-frame operations that must execute *inside* vertical blanking (raster color changes, H-scroll split effects) should be installed as callbacks: `SYS_setVIntCallback(myVBlankCallback)`.

---

### B. VDP: Planes, Tiles, Text, & Palettes
The Genesis VDP has 64 KiB VRAM, two scrolling background planes (`BG_A`, `BG_B`), and a fixed overlay plane (`WINDOW`).

- **Screen Resolution:**
  - `VDP_setScreenWidth320()` (H40 mode: 320×224 NTSC, 40 text columns).
  - `VDP_setScreenWidth256()` (H32 mode: 256×224 NTSC, 32 text columns).
- **Text & Fonts:**
  - `VDP_drawText(const char* str, u16 x, u16 y)`: Draws text at tile coordinates.
  - `VDP_clearText(u16 x, u16 y, u16 len)`: Clears string region.
  - The built-in font uses **palette color index 15**. Configure which palette row supplies font color with `VDP_setTextPalette(PAL0)`. Ensure index 15 is not black!
- **Tilemap Cell Updates:**
  - `VDP_setTileMapXY(VDPPlane plane, u16 tile_attr, u16 x, u16 y)`: Updates a single cell.
  - Tile attribute macro: `TILE_ATTR_FULL(palette, priority, vflip, hflip, tile_index)`.
  - *Performance Rule:* Never loop `VDP_setTileMapXY` across large regions; build a contiguous array in RAM and transfer via DMA or `MAP_*`.
- **Image & Tileset Blitting:**
  - `VDP_drawImage(VDPPlane plane, const Image* img, u16 x, u16 y)`: Loads palette, tileset, and tilemap bundle directly to plane.
  - `VDP_loadTileSet(const TileSet* tileset, u16 vram_index, TransferMethod tm)`.
- **Plane Scrolling:**
  - `VDP_setHorizontalScroll(VDPPlane plane, s16 value)`
  - `VDP_setVerticalScroll(VDPPlane plane, s16 value)`
  - Per-line or per-tile scrolling modes enable parallax and raster road effects.
- **Palettes (9-Bit BGR Color):**
  - 4 palettes (`PAL0`..`PAL3`) × 16 colors. Index 0 is hardware-transparent.
  - `PAL_setPalette(u16 numPal, const u16* data, TransferMethod tm)`: `tm` is usually `DMA` or `CPU`.
  - `PAL_setColor(u16 index, u16 color)`: Set individual color.
  - Convert 24-bit RGB to Genesis 9-bit BGR: `RGB24_TO_VDPCOLOR(0xRRGGBB)`.
  - Smooth hardware fades: `PAL_fadeIn()`, `PAL_fadeOut()`, `PAL_fadeTo()`.

---

### C. Sprite Engine (`SPR_*`)
Hardware provides 80 sprite slots (H40), max 20 per scanline. SGDK's sprite manager automates VRAM allocation, metasprite splitting, animations, and layering:

```c
// Allocate and spawn sprite
Sprite* s = SPR_addSprite(&hero_sprite, x, y, TILE_ATTR(PAL1, TRUE, FALSE, FALSE));
SPR_setAnim(s, ANIM_WALK);
SPR_setPosition(s, x, y);
SPR_setHFlip(s, facing_left);
SPR_setFrame(s, anim_frame);           // or SPR_setAnimAndFrame(s, anim, frame)
SPR_setVisibility(s, VISIBLE);         // toggle visibility or blink
SPR_setDepth(s, z_order);              // manages sprite priority and draw ordering
SPR_releaseSprite(s);                  // free when actor despawns
SPR_update();                          // Call ONCE per frame after all sprite updates
```
Use `SPR_addSpriteSafe()` when actor density is high to gracefully handle VRAM exhaustion.

---

### D. Input Subsystem (`JOY_*`)
```c
JOY_init();
u16 state = JOY_readJoypad(JOY_1);     // JOY_1, JOY_2, or JOY_ALL

// Check buttons
if (state & BUTTON_UP)    { /* Up */ }
if (state & BUTTON_DOWN)  { /* Down */ }
if (state & BUTTON_LEFT)  { /* Left */ }
if (state & BUTTON_RIGHT) { /* Right */ }
if (state & BUTTON_A)     { /* A */ }
if (state & BUTTON_B)     { /* B */ }
if (state & BUTTON_C)     { /* C */ }
if (state & BUTTON_START) { /* Start */ }

// Edge detection (button press vs hold)
static u16 prev_state = 0;
u16 pressed = state & ~prev_state;
prev_state = state;
```
- Non-blocking callbacks: `JOY_setEventHandler(myJoyCallback)`.
- Blocking menu helpers: `JOY_waitPress(JOY_1, BUTTON_START)` (menus only; never in game loop).
- Mask to 3-button subset (`BUTTON_A | BUTTON_B | BUTTON_C | BUTTON_START`) unless 6-button extra keys (`X`, `Y`, `Z`, `MODE`) are explicitly required.

---

### E. Direct Memory Access (`DMA_*`)
Bulk transfers between Work RAM and VRAM/CRAM/VSRAM should use DMA:
- Queue transfers during frame simulation: `DMA_queue(TransferMethod, src, dst, length, step)`.
- `SYS_doVBlankProcess()` automatically flushes the queue during VBlank.
- VBlank DMA budget is ~18 KB in NTSC. Exceeding this budget causes visible tearing or frame stalls.

---

### F. Large Scrolling Maps (`MAP_*`)
For maps larger than a 64×64 plane, use `MAP` resources and streaming decoding:
```c
Map* map = MAP_create(&dungeon_map, BG_A, TILE_ATTR_FULL(PAL0, FALSE, FALSE, FALSE, 0));
MAP_scrollTo(map, cam_x, cam_y);       // Dynamically decodes newly visible tile columns/rows
```

---

### G. Timing & System Control (`SYS_*`)
- `SYS_doVBlankProcess()`: Frame boundary sync and DMA flush.
- `SYS_setVIntCallback(callback)`: Hook interrupt code.
- `SYS_disableInts()` / `SYS_enableInts()`: Protect critical hardware sections.
- `SYS_isNTSC()` / `SYS_isPAL()`: Check 60 Hz vs 50 Hz video standard.
- `SYS_getFPS()`: Internal framerate profiler.
- `SYS_reset()`, `SYS_die(const char *msg)`: Soft reset or fatal halt with diagnostic text.

---

### H. Memory Model & Static Allocation
- **68 KiB Work RAM:** Allocate data statically or using fixed-size object pools. Avoid dynamic `malloc`/`free` to prevent heap fragmentation.
- **Keep Immutable Data in ROM:** Large tables, level structures, dialogue, and graphics belong in cartridge ROM. Read directly via pointers or `memcpy` only active slices.
- **Fixed-Point Maths (`F16` / `F32`):**
  - No hardware FPU exists on 68000; software float is unacceptably slow.
  - `F16` (10.6 fixed point) / `F32` (16.16 fixed point): `FIX16(1.5)`, `fix16Mul()`, `fix16Div()`, `sinFix16()`.
  - **Odd-Address Load Trap:** Never extract integer parts from custom struct fields using `(s16)(mem32 >> 8)`. Force through a data register:
    ```c
    static inline s16 fromfp(s32 v) { return (s16)(v >> 8); }
    ```

---

### I. Battery-Backed SRAM Saves (`SRAM_*`)
Enable SRAM in `src/rom_header.c` by setting `"RA"` and the RAM range (`0x00200000..0x002001FF`):
```c
SRAM_enable();
SRAM_writeByte(addr, value);
u8 val = SRAM_readByte(addr);
SRAM_disable();
```
Genesis SRAM resides on **odd-numbered byte addresses** (`0x200001`, `0x200003`, etc.). SGDK's API handles interleaving automatically, but raw emulator memory dumps will interleave save data with `0xFF`.

---

## 2. Sega 32X Runtime Hardware API (32XDK / Mars)

Headers: `#include "mars.h"`

### A. Core Hardware Initialization & VDP Modes
- `Mars_Init()`: Resets hardware handshakes and boots SH-2 subsystem.
- `Mars_InitVideo(int lines)`: Configures video mode (`224` or `240` scanlines).
- `Mars_InitLineTable()`: Initializes the VDP line-offset table in SDRAM.
- Video Modes:
  - `8bpp Indexed Mode`: 256 colors mapped through CRAM palette. 1 byte per pixel. Fast, compact, ideal for 2D and software-rendered 3D.
  - `15bpp Direct Color Mode`: RGB555 direct color per word. 2 bytes per pixel. No palette lookups, but halves fillrate.

### B. Framebuffer Flipping & Synchronization
The 32X has two hardware framebuffers (`0x24000000` base, 128 KiB per buffer). The back buffer is drawn while the front buffer displays:
```c
uint8_t *fb = Mars_BackBuffer();       // Obtain pointer to inactive backbuffer

// Perform drawing into fb...

Mars_FlipFrameBuffers(1);              // Request flip and wait for VBlank sync
```
- Uncached Framebuffer Writes: The framebuffer at `0x24000000` is uncached I/O memory. Writes are immediately visible to the Mars VDP.
- Line Table Doubling: The line table can point pairs of display scanlines to the same buffer row for hardware 2× vertical line doubling (`HALF_HEIGHT` mode), saving 50% of software render time.

### C. Color & Palette Control
- `Mars_SetPalette(const uint16_t *cram_data)`: Uploads 256 words of RGB555 colors to Mars CRAM.
- `Mars_SetBrightness(int level)`: Adjusts hardware video brightness (-15 to +15).

### D. Inter-CPU COMM Register Mailbox
The dual SH-2s and 68000 communicate through 16 shared 16-bit registers (`COMM0`..`COMM15`):
- `MARS_SYS_COMM0`..`COMM15`: Peripheral registers at `0x20004020` (SH-2) and `0xA15120` (68000).
- Standard Protocol:
  - Master SH-2 writes command ID to `COMM4`.
  - Slave SH-2 polls `COMM4`, executes the routine, and writes `0` (ACK).
  - Always bound the Master's wait loop to prevent hard lockups:
    ```c
    uint32_t spins = 200000;
    while (MARS_SYS_COMM4 != 0 && --spins);
    ```

### E. Controllers & Peripheral Input
- `Mars_ReadController(int port)`: Reads Genesis pad state via 68000 communication.
- Mask to 3-button subset: `state & (SEGA_CTRL_UP | SEGA_CTRL_DOWN | SEGA_CTRL_LEFT | SEGA_CTRL_RIGHT | SEGA_CTRL_A | SEGA_CTRL_B | SEGA_CTRL_C | SEGA_CTRL_START)`.
- Sega Mouse Support: `Mars_PollMouse(port)`, `Mars_ParseMousePacket()`.

---

## 3. Sega CD (Mega-CD) Runtime Architecture

### A. Dual 68000 Architecture
- **Main-CPU 68000 @ 7.67 MHz:** Controls Genesis VDP graphics, tilemaps, controllers, and sends high-level commands to Sub-CPU.
- **Sub-CPU 68000 @ 12.5 MHz:** Controls CD-ROM drive, Ricoh PCM chip, ASIC affine graphics processor, and runs game simulation/physics.

### B. Word RAM Bank Modes
The 256 KiB Word RAM can be configured in two modes:
1. **2M Mode (Bulk 256 KiB):**
   - The entire 256 KiB block is mapped exclusively to either the Main-CPU or the Sub-CPU.
   - Bank ownership is flipped via the `DMNA` and `RET` control bits in the communication register.
2. **1M Mode (Split 128 KiB Ping-Pong):**
   - Word RAM is split into two independent 128 KiB banks (Bank 0 and Bank 1).
   - While Sub-CPU renders graphics using the ASIC into Bank 0, Main-CPU DMAs Bank 1 to Genesis VRAM.
   - At VBlank, bank ownership swaps with zero wait states.

### C. ASIC Graphics Transformation Processor
The Sub-CPU drives the hardware ASIC to perform affine graphics operations:
- Bitmap rotation, scaling, skewing, and stamping.
- Processes 16×16 tile stamps from Word RAM and writes transformed scanlines back to Word RAM.
- Used for pseudo-3D driving games, rotating backgrounds, and scaling sprites.
