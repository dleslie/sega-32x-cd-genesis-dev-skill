# Resources & The `rescomp` Pipeline (SGDK & Sega Genesis)

SGDK compiles binary assets into cartridge ROM through **`rescomp`** (Java-based resource compiler). You declare assets in a `.res` descriptor file under `res/`; SGDK's `makefile.gen` runs `rescomp` on every `.res`, generating assembly source (`.s`) and a matching C header (`.h`) containing global symbols and typed structs that you `#include` directly in your code.

```
res/resources.res  -->  build/resources.s + res/resources.h  -->  #include "resources.h"
```

Each declaration occupies a single line:
```
TYPE symbol_name "relative/path/to/asset" [options...]
```
The `symbol_name` becomes a global `const` symbol of the matching SGDK C struct type.

---

## 1. Supported Resource Types

| TYPE | Generated C Type | Primary Purpose & Usage |
| :--- | :--- | :--- |
| `PALETTE` | `Palette` | 16-color palette extracted from `.pal` or the first row of an image. |
| `TILESET` | `TileSet` | Raw 8×8 tile pixel patterns (shared across multiple TILEMAPs/MAPs). |
| `TILEMAP` | `TileMap` | Uncompressed or compressed tile layout grid (good for static screens/HUDs). |
| `MAP` | `MapDefinition` | Large scrolling level map intended for dynamic camera streaming via `MAP_*`. |
| `IMAGE` | `Image` | Combined bundle containing palette, tileset, and tilemap (`VDP_drawImage`). |
| `SPRITE` | `SpriteDefinition` | Animated hardware metasprite definition consumed by `SPR_addSprite`. |
| `OBJECTS` | Custom Array | Custom Tiled map object coordinates and properties attached to a `MAP`. |
| `XGM` | `const u8[]` | Legacy XGM music track compiled via `xgmtool`. |
| `XGM2` | `const u8[]` | Modern multi-track XGM2 music track compiled via `xgm2tool`. |
| `WAV` | `const u8[]` | 8-bit unsigned PCM sound effect or streamed voice sample. |
| `BIN` | `const u8[]` | Arbitrary raw binary blob embedded verbatim into ROM. |

---

## 2. Declarations Reference & Examples

```res
# 1. Palette: Extract 16 hardware colors from image or .pal file
PALETTE pal_dungeon "gfx/dungeon.pal"

# 2. Shared Tileset: Optimize by dropping duplicate and flipped tiles
TILESET tiles_dungeon "gfx/tiles.png" BEST ALL

# 3. Streamable Large Level Map: Links to shared tileset, optimizing tile references
MAP map_dungeon "gfx/dungeon_layout.png" tiles_dungeon BEST 0

# 4. Standalone Fullscreen Background Image: Palette + Tileset + Tilemap bundle
IMAGE img_title "gfx/title_screen.png" BEST

# 5. Animated Sprite: Frame dimensions in TILES (8px units), compression, and tick pacing
# Parameters: <path> <width_in_tiles> <height_in_tiles> <compression> <anim_speed_ticks>
SPRITE spr_hero "sprites/hero.png" 4 4 FAST 6

# 6. Audio: Modern XGM2 chiptune music track (runs xgm2tool internally)
XGM2 bgm_cathedral "music/cathedral.vgm"

# 7. Sound Effects: 8-bit mono WAV, targeted driver, and sample rate in Hz
# Parameters: <path> <driver: XGM2|XGM> <sample_rate_hz>
WAV sfx_sword "audio/sword_swing.wav" XGM2 11025

# 8. Raw Data: Byte-for-byte binary inclusion (level arrays, precomputed tables)
BIN data_items "data/items.bin"
```

---

## 3. Compression Flags & Options

For resources that accept compression (`IMAGE`, `TILESET`, `TILEMAP`, `MAP`, `SPRITE`):

| Flag | Decompression Speed | Compression Ratio | Best Use Case |
| :--- | :--- | :--- | :--- |
| `NONE` | Instant (no CPU overhead) | 1:1 (uncompressed) | Streamed tiles, fast random access, fonts |
| `FAST` / `LZ4W` | Very fast (C decompressor) | Moderate (~50%) | Level loads, animated sprite frames |
| `APLIB` | Slow (high cycle cost) | High (~30–40%) | Large title screens, memory-constrained ROMs |
| `BEST` / `AUTO` | Chooses optimal ratio | Automatic | General static backgrounds |

To decompress assets at runtime, use SGDK helpers:
- `unpackTileSet(const TileSet *src, TileSet *dst)`
- `unpackImage(const Image *src, Image *dst)`

---

## 4. `TILESET` Optimization Flags

The `opt` parameter controls how `rescomp` handles redundancy across 8×8 tiles:
- `ALL` (Default): Discards exact duplicate tiles AND tiles that can be represented by horizontally or vertically flipping an existing tile. Highly recommended for scenery.
- `DUPLICATE`: Discards exact duplicates only; preserves distinct rotations/flips.
- `NONE`: Preserves every single tile verbatim in layout order. Essential for fonts, ASCII text character sheets, and ordered HUD glyphs where tile indices must correspond directly to character codes.

---

## 5. File Directives

Place these directives on standalone lines within your `.res` file:
- `ALIGN <bytes>`: Enforces memory alignment for subsequent resources (e.g., `ALIGN 2` for word alignment, `ALIGN 4` for 32-bit alignment).
- `UNGROUP`: Separates resource symbols so the linker's `--gc-sections` can strip unused assets individually.
- `NEAR`: Forces the resource into the low (<4 MiB) ROM window before any bankswitching boundary. Crucial for interrupt handlers, font tilesets, and boot graphics.

---

## 6. Image & Palette Formatting Rules

To avoid distorted colors, black screens, or linker rejections:

1. **Indexed Images (1, 2, 4, 8 bpp PNG/BMP):**
   - `rescomp` extracts the indexed color palette directly from the image header.
   - **Color index 0 is always transparent** for sprites and plane tiles. Order your color palette in Aseprite/GIMP such that transparency is mapped to index 0.
   - For 8bpp tilesets, colors with index $\ge 128$ can be configured to encode high-priority tile attributes.

2. **Truecolor (24-bit RGB) Images:**
   - Because 24-bit images lack embedded indexed palettes, you must embed a **palette bar in the top-left corner**:
     - Each color swatch must occupy an 8×8 tile.
     - Each 16-color palette row must occupy its own horizontal scanline of tiles.
     - **All four palette rows (64 color tiles total) must be present**, even if rows 2–4 are unused.
     - `rescomp` reads this top-left grid as palettes `PAL0..PAL3` and strips the area from the actual graphic before converting tiles.

3. **9-Bit BGR Color Precision:**
   - Genesis hardware only supports 9-bit BGR (3 bits per color channel, 512 possible colors).
   - RGB color values are clamped: channel values $0..255$ map to eight quantized levels ($0, 36, 73, 109, 146, 182, 219, 255$).
   - Never hand-multiply 6-bit VGA DAC colors ($0..63$) by 4; use proper 3-bit shifts (`(val >> 3) & 0x7`).

4. **Metasprite Geometry:**
   - Hardware sprites support sizes from $1\times 1$ up to $4\times 4$ tiles ($8\times 8$ to $32\times 32$ pixels).
   - Declaring a sprite larger than $4\times 4$ (e.g., $6\times 6$ tiles = $48\times 48$ px) causes `rescomp` to split the frame into multiple hardware sprite components linked in a `SpriteDefinition`.

---

## 7. Consuming Resources in C

Include the generated header and call SGDK runtime APIs:

```c
#include <genesis.h>
#include "resources.h"

void init_game_scene(void)
{
    // 1. Load 16-color palette into hardware PAL0 using DMA
    PAL_setPalette(PAL0, pal_dungeon.data, DMA);

    // 2. Draw fullscreen title image to Background Plane A
    VDP_drawImage(BG_A, &img_title, 0, 0);

    // 3. Initialize dynamic scrolling map on Plane B
    Map *level_map = MAP_create(&map_dungeon, BG_B, TILE_ATTR_FULL(PAL0, FALSE, FALSE, FALSE, 0));
    MAP_scrollTo(level_map, 0, 0);

    // 4. Spawn an animated hardware metasprite
    Sprite *player = SPR_addSprite(&spr_hero, 160, 100, TILE_ATTR(PAL1, TRUE, FALSE, FALSE));
    SPR_setAnim(player, 0);

    // 5. Start background chiptune music track (loops automatically)
    XGM2_play(bgm_cathedral);

    // 6. Trigger PCM sound effect on PCM Channel 1
    XGM2_playPCM(sfx_sword, sizeof(sfx_sword), SOUND_PCM_CH1);

    // 7. Read raw binary table directly from ROM
    const uint8_t *item_blob = data_items;
}
```
