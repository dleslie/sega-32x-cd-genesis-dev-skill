# Sega Mega Drive / Genesis VDP Graphics & Visual Effects Manual
## Comprehensive Hardware Engine, Priority Matrix, Raster Effects, and Original Implementation Playbook

The Sega Genesis / Mega Drive Video Display Processor (VDP) is a custom ASIC (derived from the Yamaha YM7101 / Texas Instruments TMS9918 lineage) tailored for high-speed tilemap scrolling, sprite rasterization, and hardware color operations. Unlike later consoles (such as the Super Nintendo with Mode 7 or the Sega CD with its affine ASIC), the Genesis VDP possesses **no native hardware rotation, scaling, or arbitrary affine transformation units**. 

Every legendary visual effect on the Mega Drive—twisting cylindrical towers, rolling 3D roads, undulating water reflections, translucent spotlights, and multi-jointed behemoths—was achieved through creative manipulation of the VDP's core subsystems:
1. **Per-scanline Horizontal Scrolling (`HSRAM`)**
2. **2-Cell Column Vertical Scrolling (`VSRAM`)**
3. **Mid-frame Horizontal Raster Interrupts (`H-INT`)**
4. **Hardware Shadow and Highlight Mode (`S/H`)**
5. **Hierarchical Kinematic Sprite Linkages**
6. **Composite Video Chroma Blending**

This manual covers the complete mechanics, memory architectures, priority rules, and algorithmic implementations for these techniques.

---

## 1. VDP Hardware Architecture & Memory Layout

```
┌────────────────────────────────────────────────────────────────────────┐
│                        GENESIS VDP MEMORY SPACES                       │
├────────────────────────────────┬───────────────────────────────────────┤
│ VRAM (Video RAM - 64 KB)       │ 16-bit word wide (32K × 16 bits)      │
│                                │ Holds Tile Patterns, Plane Tables,    │
│                                │ Sprite Attribute Table, H-Scroll Table│
├────────────────────────────────┼───────────────────────────────────────┤
│ CRAM (Color RAM - 128 Bytes)   │ 64 color words (4 palettes × 16 colors)│
│                                │ 9-bit BGR format (3 bits per channel) │
├────────────────────────────────┼───────────────────────────────────────┤
│ VSRAM (Vert Scroll RAM - 80 B) │ 40 scroll words (20 columns in H40)   │
│                                │ Controls vertical offset per 16-pixel │
│                                │ strip across Plane A and Plane B      │
└────────────────────────────────┴───────────────────────────────────────┘
```

### Tile Architecture & Data Format
- All graphics are fundamentally composed of **$8 \times 8$ pixel tiles** (patterns).
- Each pixel is encoded as a **4-bit color index** ($0..15$).
- One tile requires **32 bytes** ($8 \times 8 \times 4 \text{ bits} / 8 = 32 \text{ bytes}$).
- In memory, each 16-bit word stores 4 pixels (one scanline of a tile):
  ```
  Bit:   15 14 13 12 | 11 10  9  8 |  7  6  5  4 |  3  2  1  0
  Pixel:  Pixel 0    |  Pixel 1    |  Pixel 2    |  Pixel 3
  ```
- Color index `0` of any palette is treated as **transparent** by planes and sprites (revealing lower layers or the backdrop color).

### Plane Pattern Name Tables
A plane name table maps VRAM tile indexes across a virtual grid ($64 \times 32$, $32 \times 64$, or $64 \times 64$ cells). Each cell is represented by a single **16-bit word**:

```
Bit 15:    Priority flag (0 = Low Priority, 1 = High Priority)
Bits 14-13: Palette select (00 = Pal 0, 01 = Pal 1, 10 = Pal 2, 11 = Pal 3)
Bit 12:    Vertical flip (0 = Normal, 1 = Inverted vertically)
Bit 11:    Horizontal flip (0 = Normal, 1 = Inverted horizontally)
Bits 10-0: Tile Index in VRAM (0..2047)
```

### Sprite Attribute Table (SAT)
The VDP renders up to **80 sprites** per frame (up to 20 per scanline in H40 mode, 16 in H32 mode). Each sprite entry in the SAT consumes **8 bytes**:

```
Word 0:  xxxxxx yyyyyyyyy  (Y coordinate + 128 bias)
Word 1:  ---- ww hh - llllll (w = width-1 in tiles [0..3], h = height-1 in tiles [0..3], l = link pointer to next sprite)
Word 2:  p cc v h tttttttttt (p = priority, cc = palette, v = V-flip, h = H-flip, t = base tile index)
Word 3:  xxxxxx xxxxxxxxx  (X coordinate + 128 bias)
```

---

## 2. The Comprehensive Priority Ladder

Understanding how the VDP decides which pixel appears on the television is vital for transparency, silhouette rendering, and multi-layer depth.

```
       [FRONT OF SCREEN]
              │
              ▼
   1. High-Priority Sprite Pixels (Color ≠ 0)
              │
              ▼
   2. High-Priority Window Plane Pixels (Color ≠ 0)
              │
              ▼
   3. High-Priority Plane A Pixels (Color ≠ 0)
              │
              ▼
   4. High-Priority Plane B Pixels (Color ≠ 0)
              │
              ▼
   5. Low-Priority Sprite Pixels (Color ≠ 0)
              │
              ▼
   6. Low-Priority Window Plane Pixels (Color ≠ 0)
              │
              ▼
   7. Low-Priority Plane A Pixels (Color ≠ 0)
              │
              ▼
   8. Low-Priority Plane B Pixels (Color ≠ 0)
              │
              ▼
   9. Backdrop Color (Register 7: Palette 0..3, Index 0..15)
              │
       [BACK OF SCREEN]
```

### Key Priority Takeaways
- **Foreground Interleaving**: Setting a background tile to High Priority causes it to obscure **Low-Priority Sprites** (e.g. a character walking behind trees or pillars), while still allowing **High-Priority Sprites** (e.g. foreground UI, flying birds, or bullet sparks) to appear on top.
- **Window Plane Exclusivity**: The Window plane overrides Plane A within its defined rectangular coordinates; it does not mix with Plane A, but occupies its slot in the priority hierarchy.

---

## 3. Advanced Scrolling Techniques

The Genesis VDP provides independent horizontal and vertical scroll mechanisms with multiple granularities.

### Summary of Scroll Modes (VDP Registers 11 & 12)

| Dimension | Mode | Granularity | Storage Location | Application |
| :--- | :--- | :--- | :--- | :--- |
| **Horizontal** | Full-Screen | Whole Screen | 2 words in VRAM | Standard camera panning |
| **Horizontal** | Cell (Row) | 8-pixel horizontal bands | 64 words in VRAM | Stepped parallax (e.g. skies) |
| **Horizontal** | Line Scroll | 1-pixel scanline | 448 words in VRAM (H40) | Raster waves, pseudo-3D roads, heat shimmer |
| **Vertical** | Full-Screen | Whole Screen | 2 words in VSRAM | Standard vertical camera panning |
| **Vertical** | 2-Cell Column | 16-pixel vertical strips | 40 words in VSRAM (H40) | Waterfall warping, rolling cylinders |

---

### Implementation A: Per-Line Pseudo-3D Perspective Ground Plane
By assigning an independent horizontal scroll offset to every scanline of Plane B, a flat textured tilemap can be projected into a 3D floor or ceiling receding toward an artificial horizon.

```
Scanline (y)                          H-Scroll Offset (Plane B)
y = 120 (Horizon) ──────────────► dx = 0  (Stationary vanishing point)
y = 140           ──────────────► dx = ± 12
y = 160           ──────────────► dx = ± 28
y = 180           ──────────────► dx = ± 54
y = 200           ──────────────► dx = ± 96
y = 224 (Nearest) ──────────────► dx = ± 160 (High-speed foreground rush)
```

#### Hyperbolic Perspective Formulation
To simulate correct linear camera projection, the horizontal displacement $S(y)$ for scanline $y$ (where $y > y_{\text{horizon}}$) follows a reciprocal depth function:

$$S(y) = X_{\text{camera}} + \frac{Z_{\text{camera}} \cdot X_{\text{speed}}}{y - y_{\text{horizon}}}$$

```c
#include <genesis.h>

#define SCREEN_HEIGHT 224
#define HORIZON_Y     110

static s16 hscroll_table[SCREEN_HEIGHT];
static fix16 camera_x = 0;
static fix16 camera_speed = FIX16(2.5);

void update_perspective_road(void)
{
    camera_x = fix16Add(camera_x, camera_speed);
    s16 base_x = fix16ToInt(camera_x);

    /* Upper screen: Static sky / mountains */
    for (int y = 0; y < HORIZON_Y; y++)
    {
        hscroll_table[y] = -(base_x >> 3); /* Slow distant scroll */
    }

    /* Lower screen: Hyperbolic depth floor */
    for (int y = HORIZON_Y; y < SCREEN_HEIGHT; y++)
    {
        int depth = y - HORIZON_Y + 1;
        /* Perspective scaling factor: faster movement near screen bottom */
        s16 road_offset = (base_x * depth) >> 5;
        hscroll_table[y] = -road_offset;
    }

    /* DMA update H-Scroll table in VRAM for Plane B */
    VDP_setHorizontalScrollTile(BG_B, 0, hscroll_table, SCREEN_HEIGHT, DMA);
}
```

---

### Implementation B: Multi-Layer Sine Wave Water Distortion
By modulating the line scroll table with a periodic trigonometric function, Plane A can be warped into fluid underwater ripple reflections:

$$S(y, t) = \text{Scroll}_{\text{base}} + A \cdot \sin\left(\frac{2\pi \cdot y}{\lambda} + \omega \cdot t\right)$$

```c
#include <genesis.h>

#define WATER_START_Y 130
#define WATER_HEIGHT  (224 - WATER_START_Y)

static s16 water_hscroll[WATER_HEIGHT];
static u16 wave_phase = 0;

void update_underwater_ripple(s16 base_camera_x)
{
    wave_phase += 4; /* Advance oscillation */

    for (int i = 0; i < WATER_HEIGHT; i++)
    {
        /* Compute sine offset using SGDK's lookup table (sinFix16) */
        u16 angle = (i * 8 + wave_phase) & 1023;
        s16 sine_val = fix16ToInt(sinFix16(angle) * 6); /* Amplitude = 6 pixels */

        water_hscroll[i] = base_camera_x + sine_val;
    }

    /* Write line offsets for the water segment of Plane A */
    VDP_setHorizontalScrollLine(BG_A, WATER_START_Y, water_hscroll, WATER_HEIGHT, DMA);
}
```

---

### Implementation C: 16-Pixel Column Scrolling (VSRAM) for Rolling Cylinders
Vertical Scroll RAM (`VSRAM`) contains 40 words in H40 mode. Words $0, 2, 4 \dots 38$ dictate the vertical shift for successive 16-pixel wide vertical columns of Plane A, while words $1, 3, 5 \dots 39$ dictate Plane B.

```c
#include <genesis.h>

#define NUM_COLUMNS 20 /* 320 pixels / 16 pixels per column = 20 columns */

static s16 vsram_buffer[NUM_COLUMNS];
static u16 cylinder_step = 0;

void update_vertical_column_wave(void)
{
    cylinder_step += 6;

    for (int col = 0; col < NUM_COLUMNS; col++)
    {
        /* Modulate column vertical offset via phase-shifted cosine */
        u16 angle = (col * 32 + cylinder_step) & 1023;
        vsram_buffer[col] = fix16ToInt(cosFix16(angle) * 12); /* 12-pixel displacement */
    }

    /* Transfer 20 words to VSRAM for Plane A */
    VDP_setVerticalScrollTile(BG_A, 0, vsram_buffer, NUM_COLUMNS, DMA);
}
```

---

## 4. Hardware Shadow & Highlight Mode (S/H): The 153-Color Pipeline

The Mega Drive is widely cited as having a 64-color palette ($4 \times 16$). However, the VDP hardware includes an integrated analog luminance operator called **Shadow and Highlight Mode** (`S/H`). When activated, the VDP dynamically scales the analog RGB DAC output per pixel, yielding **up to 153 simultaneously displayable colors**.

### 1. Activating S/H Mode
Write bit 3 (`SH`) of VDP Register 12 (`0x8C`):
```c
/* Enable Mode H40 (320x224) + Shadow/Highlight (bit 3) */
VDP_setReg(0x0C, 0x81 | 0x08); 
```

### 2. The Color Math Matrix

| Mode Level | Relative Luminance | Transformation Formula | Usable Range |
| :--- | :--- | :--- | :--- |
| **Shadow** | **$0.5 \times$ Brightness** | Each color channel is halved: $C_{\text{sh}} = \lfloor C / 2 \rfloor$ | Darkens shadows, nighttime, tunnels |
| **Normal** | **$1.0 \times$ Brightness** | Direct CRAM RGB value: $C_{\text{norm}} = C$ | Standard rendering |
| **Highlight** | **$2.0 \times$ Brightness** | Each channel shifted up: $C_{\text{hi}} = \min(14, C + 8)$ (or $C/2 + 7$ on dark tones) | Explosions, sun rays, spotlights |

### 3. Operator Triggers & Sprite Interaction Rules
Shadow and Highlight are triggered through priority and palette channel selection:

```
┌────────────────────────────────────────────────────────────────────────┐
│                        SHADOW / HIGHLIGHT LOGIC                        │
├──────────────────────────────┬─────────────────────────────────────────┤
│ Low-Priority Background Tile │ Rendered in SHADOW (half luminance)     │
│ (No high-priority layer over)│ unless illuminated by a sprite.         │
├──────────────────────────────┼─────────────────────────────────────────┤
│ High-Priority Background Tile│ Rendered in NORMAL (full luminance).    │
├──────────────────────────────┼─────────────────────────────────────────┤
│ Sprite Palette 3, Color 15   │ TRANSPARENT SHADOW OPERATOR:            │
│                              │ Does not draw solid pixels; forces all  │
│                              │ pixels underneath into SHADOW mode.     │
├──────────────────────────────┼─────────────────────────────────────────┤
│ Sprite Palette 3, Color 14   │ TRANSPARENT HIGHLIGHT OPERATOR:         │
│                              │ Does not draw solid pixels; forces all  │
│                              │ pixels underneath into HIGHLIGHT mode.  │
├──────────────────────────────┼─────────────────────────────────────────┤
│ Sprite Palette 0, 1, 2 or    │ Solid pixels drawn at NORMAL brightness │
│ Palette 3 (Colors 1..13)     │ when High-Priority; drawn in SHADOW     │
│                              │ if Low-Priority over shadowed planes.   │
└──────────────────────────────┴─────────────────────────────────────────┘
```

### Original Example: Dynamic Flashlight Spotlight Cone
To create a moving cone of light in a pitch-black dungeon:
1. Render all background tiles (walls, floors) with **Low Priority** (`Priority = 0`). The entire screen appears automatically dimmed to half-brightness (Shadow).
2. Create a triangular/circular spotlight sprite assigned to **Palette 3, Color 14**.
3. Position the sprite over the player's lantern. Every tile pixel caught within the sprite's footprint is boosted to **Normal** or **Highlight** brightness, creating a dynamic illuminated spotlight with **zero CPU raster cycle overhead**.

---

## 5. Transparency, Dithering, & Visual Illusions

### A. Checkerboard Dither Blending via Composite Artifacts
On standard North American and European CRT televisions connected via **Composite RCA (yellow cable) or RF switch**, the Genesis's 3.58 MHz NTSC chroma subcarrier overlaps with the VDP's high-frequency pixel clock ($7.67 \text{ MHz}$ in H40 mode). 

High-frequency checkerboard patterns are bandwidth-limited by the analog comb filter, causing alternating pixels to smear horizontally into **solid, translucent gradients**:

```
Dither Pattern Matrix (50% Transparency):
Scanline 0:  [■] [ ] [■] [ ] [■] [ ] [■] [ ]
Scanline 1:  [ ] [■] [ ] [■] [ ] [■] [ ] [■]
Scanline 2:  [■] [ ] [■] [ ] [■] [ ] [■] [ ]
Scanline 3:  [ ] [■] [ ] [■] [ ] [■] [ ] [■]
```

- **When to Use**: Fog, waterfall overlays, glass windows, shadows, energy shields.
- **RGB Cleanliness Warning**: On modern emulators, RGB SCART cables, and FPGA systems, dithering does not smear and displays as raw mesh pixels. To maintain clean visuals across both display types, combine dithering with harmonious tone pairings (e.g. alternating medium blue with dark blue, rather than black with neon yellow).

### B. 30 Hz Temporal Flicker Transparency
When true hardware transparency is unavailable and S/H mode is tied up by background lighting:
1. Assign translucent entities (smoke, forcefields, invulnerability flashes) to a dedicated object list.
2. Toggle their visibility bit in the SAT on alternating frames:
   ```c
   if (frame_counter & 1)
   {
       /* Hide sprite by placing offscreen (Y = 0) */
       VDP_setSpritePosition(shield_sprite_id, -128, -128);
   }
   else
   {
       /* Display sprite over player */
       VDP_setSpritePosition(shield_sprite_id, player_x, player_y);
   }
   ```
On 60 Hz CRT monitors, phosphor decay and human optical persistence blend the alternating frames into a convincing ~50% translucent overlay.

---

## 6. Horizontal Raster Interrupts (`H-INT`) & Mid-Frame Palette Swapping

The Genesis 68000 can synchronize operations with the scanning electron beam using the **Horizontal Interrupt** (`H-INT`).

### 1. Register Configuration & Timing
- **VDP Register 10 (`0x8Axx`)**: Line Interrupt Counter. Defines how many scanlines elapse between H-INT triggers ($0 = \text{every line}$, $1 = \text{every 2 lines}$, etc.).
- **VDP Register 00 (`0x80xx`)**: Bit 4 must be set to `1` to enable H-INT dispatch to the 68000.
- **Timing Window**: The 68000 receives the interrupt at the end of active display. It has approximately **32–36 CPU clock cycles** during HBlank before the beam reaches the next active scanline.

```
Active Scanline (Pixels Rendered) ──► [HBlank: ~32 cycles] ──► Next Scanline
                                               ▲
                                      H-INT Handler Executes Here
                                      (Update CRAM / VSRAM / Regs)
```

### 2. Implementation: Two-Tone Water Horizon Horizon Palette Split
By rewriting Color RAM (`CRAM`) during HBlank at a specific scanline, games can present vibrant outdoor landscapes above and rich deep-sea palettes below without needing multiple consoles or extra chips.

```c
#include <genesis.h>

#define WATERLINE_SCANLINE 112

static u16 s_water_palette[16] = {
    0x0000, 0x0420, 0x0640, 0x0862, 0x0A84, 0x0CA6, 0x0EC8, 0x0444,
    0x0666, 0x0888, 0x0AAA, 0x0220, 0x0440, 0x0662, 0x0884, 0x0EEA
};

/* H-INT Callback executed during horizontal blanking */
static void hInt_waterline_callback(void)
{
    u16 current_line = VDP_getHCounter();

    if (current_line >= WATERLINE_SCANLINE)
    {
        /* Push blue-tinted marine palette into CRAM for Palette 0 */
        VDP_setPaletteColors(PAL0, s_water_palette, 16);
    }
}

/* V-INT Callback executed at start of frame (VBlank) */
static void vInt_reset_palette_callback(void)
{
    /* Restore normal above-water sunny palette at top of screen */
    VDP_setPaletteColors(PAL0, palette_sunny_day.data, 16);
}

void init_waterline_raster_effect(void)
{
    /* Set H-INT interval: trigger on target line */
    VDP_setHIntCounter(WATERLINE_SCANLINE);
    VDP_setHInterrupt(1);

    SYS_setHIntCallback(hInt_waterline_callback);
    SYS_setVIntCallback(vInt_reset_palette_callback);
}
```

---

## 7. Geometric Transformations: Affine Shearing & Fake 2D Rotation

Because the VDP cannot rotate planes in hardware, rotation is approximated using **Affine Horizontal Shearing**:

$$\begin{bmatrix} x' \\ y' \end{bmatrix} = \begin{bmatrix} 1 & \tan(\theta) \\ 0 & 1 \end{bmatrix} \begin{bmatrix} x \\ y \end{bmatrix} \implies x' = x + y \cdot \tan(\theta)$$

By applying a progressive, linear horizontal shift to each scanline of Plane A:
- A square tilemap is sheared into a parallelogram.
- Combining shear with pre-rotated sprite frames or alternating plane shifts creates the illusion of **rotating cylindrical towers** (e.g. *Castlevania: Bloodlines*, *Contra: Hard Corps*).

```
Unsheared Plane (Angle = 0°)           Sheared Plane (Angle = 20°)
┌──────────────────────────┐           ┌──────────────────────────┐
│                          │            \                          \
│                          │             \                          \
│                          │              \                          \
└──────────────────────────┘               └──────────────────────────┘
```

```c
#include <genesis.h>

#define SCREEN_HEIGHT 224

static s16 shear_table[SCREEN_HEIGHT];
static fix16 shear_angle = 0;

void update_cylindrical_shear(void)
{
    shear_angle = fix16Add(shear_angle, FIX16(0.05));
    fix16 tan_val = sinFix16(fix16ToInt(shear_angle * 100) & 1023);

    for (int y = 0; y < SCREEN_HEIGHT; y++)
    {
        /* Center origin at middle of screen (y - 112) */
        int centered_y = y - (SCREEN_HEIGHT / 2);
        shear_table[y] = fix16ToInt(fix16Mul(tan_val, FIX16(centered_y)));
    }

    /* Apply shear table to Plane A */
    VDP_setHorizontalScrollLine(BG_A, 0, shear_table, SCREEN_HEIGHT, DMA);
}
```

---

## 8. Multi-Jointed Segmented Characters (Hierarchical Kinematics)

### The Large Boss VRAM Problem
- A monolithic $128 \times 128$ pixel boss requires **64 VRAM tiles** ($2 \text{ KB}$).
- Animating that boss at 60 FPS across 10 frames requires **640 tiles** ($20 \text{ KB}$), exceeding standard VRAM allocations and blowing the VBlank DMA bandwidth budget (~7.6 KB).

### The Kinematic Solution
Rather than swapping large tile bitmaps in VRAM:
1. Store a handful of small, modular sprite parts in VRAM (a head, a claw, an eyeball, 3 body segment links). These take **less than 1 KB of VRAM total** and are loaded once at level boot.
2. Link the segments in a mathematical skeletal tree (hierarchical forward kinematics).
3. Update only the **X and Y positions in the Sprite Attribute Table** at 60 FPS:

$$x_n = x_{n-1} + L \cdot \cos(\theta_n), \quad y_n = y_{n-1} + L \cdot \sin(\theta_n)$$

```
  [Segment 0: Base]
         │
         │ (Length L, Angle θ₁)
         ▼
  [Segment 1: Body]
         │
         │ (Length L, Angle θ₂)
         ▼
  [Segment 2: Body]
         │
         │ (Length L, Angle θ₃)
         ▼
  [Segment 3: Mandible / Head]
```

```c
#include <genesis.h>

#define NUM_SEGMENTS 6
#define LINK_LENGTH  18

typedef struct {
    fix16 x, y;
    u16 angle;
    u16 sprite_id;
} SerpentSegment;

static SerpentSegment serpent[NUM_SEGMENTS];
static u16 global_motion = 0;

void init_serpent(void)
{
    for (int i = 0; i < NUM_SEGMENTS; i++)
    {
        serpent[i].x = FIX16(160);
        serpent[i].y = FIX16(112 + i * LINK_LENGTH);
        serpent[i].angle = 0;
        serpent[i].sprite_id = i;
    }
}

void update_serpent_kinematics(fix16 target_x, fix16 target_y)
{
    global_motion += 8;

    /* Lead segment tracks target / player */
    serpent[0].x = target_x;
    serpent[0].y = target_y;

    /* Subsequent segments follow using kinematic trail formulas */
    for (int i = 1; i < NUM_SEGMENTS; i++)
    {
        /* Modulate joint angle with harmonic wave */
        u16 wave = (global_motion + i * 90) & 1023;
        serpent[i].angle = wave;

        /* Position relative to previous segment */
        fix16 dx = cosFix16(wave) * LINK_LENGTH;
        fix16 dy = sinFix16(wave) * LINK_LENGTH;

        serpent[i].x = fix16Add(serpent[i - 1].x, dx);
        serpent[i].y = fix16Add(serpent[i - 1].y, dy);

        /* Write updated position to Sprite Attribute Table */
        VDP_setSpritePosition(serpent[i].sprite_id, 
                              fix16ToInt(serpent[i].x), 
                              fix16ToInt(serpent[i].y));
    }

    VDP_updateSprites(NUM_SEGMENTS, DMA);
}
```

---

## 9. DMA Bandwidth Budgets & VBlank Scheduling

### The Hardware Bandwidth Envelope
The VDP cannot receive DMA transfers while the raster gun is actively painting the screen without visual artifacts and bus lockups. Transfers must occur during **Vertical Blanking (VBlank)**.

| Metric | NTSC (60 Hz, 224 Lines) | PAL (50 Hz, 240 Lines) |
| :--- | :--- | :--- |
| **Active Scanlines** | 224 scanlines | 240 scanlines |
| **VBlank Scanlines** | **38 scanlines** | **72 scanlines** |
| **Max Safe DMA Bandwidth** | **~7.6 KB per frame** | **~14.4 KB per frame** |
| **Max 4bpp Tiles Uploaded** | **~240 tiles per frame** | **~450 tiles per frame** |

### The DMA Queue Rule
Never issue synchronous DMA calls in the middle of game logic. Always queue transfers into the SGDK DMA manager during gameplay, and flush the queue in a single burst immediately upon entering VBlank:

```c
/* During gameplay logic (Frame N): */
DMA_queueDma(DMA_VRAM, (u32)source_tiles, VRAM_TILE_DEST, num_words, 2);

/* At end of frame: */
SYS_doVBlankProcess(); /* Flushes DMA queue safely within the 38 VBlank scanlines */
```
