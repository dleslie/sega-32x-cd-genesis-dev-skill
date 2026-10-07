# SH-2 and 68000 Optimization Playbook (Genesis, 32X, & Sega CD)

Apply these rules when profiling shows performance bottlenecks, framerate drops, or when optimizing for 60 fps. Always prioritize correctness first: optimize against green automated test suites (`test-host`, headless emulator tests) to prove no behavior or visual regressions occur.

---

## 1. The SH-2 Cost Model & Architecture Realities

- **No Fast Hardware Divide:** Division (`/`) and modulo (`%`) on the SH-2 do not have single-cycle hardware instructions. 32-bit division requires 30–40 cycles; 64-bit division emits slow libgcc `__divdi3` helper calls. Avoid runtime divides in loops entirely.
- **Multiplication is Cheap-ish; Shifts are Cheapest:** Single-cycle 32-bit multiplication (`dmuls.l`, `mul.l`) is fast, but bitwise shifts (`shll`, `shlr`, `shld`) are single-cycle and zero-cost when scheduled. Always prefer `<<`, `>>`, `&` over `*`, `/`, `%` by powers of two.
- **Small On-Chip Cache (4 KiB per Core), Massive SDRAM Penalty:** Each SH-2 has a 4 KiB direct-mapped cache. A cache miss to SDRAM incurs 10–14 bus wait cycles. Keep inner loops tight and data structures cache-line aligned (16 bytes).
- **Two Hitachi SH-2 Cores @ ~23 MHz:** Offloading independent work to the Slave SH-2 immediately halves elapsed wall-clock time for that phase.
- **Fillrate & Framebuffer Bottlenecks:** The 32X framebuffer at `0x24000000` is uncached I/O memory. Writing 320×224 pixels (~72 KiB) consumes significant bus cycles. Fillrate and overdraw are far more often the performance wall than CPU arithmetic.

---

## 2. Core Optimization Rules (Priority Order)

### 1. Hoist Invariant Calculations Out of Loops
Compute anything that does not change per iteration once before the loop begins:
- Array base pointers, stride calculations, and struct member offsets.
- Camera basis vectors, perspective focal scalars, and lighting direction dot products.
- Function calls that return constant values per frame.

### 2. Precompute Lookup Tables (LUTs) in ROM
Never compute trigonometric functions, logarithms, color blends, or division at runtime:
- Trig (`sin_table`, `cos_table`, `atan2_table`).
- Reciprocals for projection division (`recip_table`).
- Color ramps, palette translation tables (TRN), and shade maps.
- Pack tables into ROM as `const` arrays so they consume zero SDRAM.

### 3. Bit-Shift and Mask Instead of Division/Modulo
- Replace `x / 64` with `x >> 6`.
- Replace `x % 64` with `x & 63`.
- Replace `x * 8` with `x << 3`.
- Divide by non-power-of-two constants via precomputed reciprocal multiplication and shifts.

### 4. Use Fixed-Point Arithmetic (Never Float) in Hot Paths
- Standard format is **16.16 fixed point** (`int32_t` with 16 fractional bits).
- Multiplication: `(int32_t)(((int64_t)a * b) >> 16)`.
- Core simulation must be float-free so desktop host tests and console builds produce bit-identical results.

### 5. Offload Discrete Work Phases to the Slave SH-2
Split frame processing into stages and let the Slave SH-2 execute one while Master SH-2 processes the next:
- Slave Stage: Audio mixing (PWM), framebuffer clearing, floor/ceiling plane rasterization, or raycasting columns.
- Master Stage: Game simulation, AI ticks, collision detection, and sprite rasterization.
- Synchronize via `COMM` register handshakes with bounded wait timeouts.

### 6. Shrink the Work, Not Just the Code
- **Render Narrow / Lower Resolution:** Render an internal buffer of 160×112 and let hardware line-doubling expand it to 320×224.
- **Cap Draw Distance & Frustum Cull Early:** Discard offscreen vertices and objects before performing sorting or rasterization setup.
- **Cull Backfaces and Occluded Spans:** Never write pixels that will be overwritten.

### 7. Reduce Cache Pressure & Align Memory
- Align hot buffers and DMA structures to **16-byte cache line boundaries**.
- Mark shared routines with cache alignment attributes:
  ```c
  #define ATTR_DATA_CACHE_ALIGN __attribute__((section(".sdata"), aligned(16), optimize("O2")))
  ```
- Keep hot data structures compact to fit inside 4 KiB cache.

### 8. Batch Writes Using 32-Bit Aligned Transfers
- In 8bpp indexed mode, never write single bytes in loops. Pack four 8-bit pixels into a 32-bit `uint32_t` word and store using aligned 32-bit writes (`mov.l`).
- Reduces memory store transactions by 4×.

---

## 3. Eliminating the Runtime Divide (Software 3D & Projection)

The perspective divide `screen_x = (world_x * focal) / z` is the classic projection bottleneck. Replace runtime division with a **reciprocal table**:

### Precomputing the Reciprocal Table
Generate a table at build time covering the valid depth range:
```c
// RECIP_SH = 22, z shifted by 12 bits to form index k
#define RECIP_SH 22
const uint32_t recip_table[4096] = { /* (1 << RECIP_SH) / k */ };
```

### Fast Projection Multiply
```c
uint32_t k = (uint32_t)(z >> 12);
if (k < 4096 && k > 0) {
    uint32_t inv_z = recip_table[k];
    screen_x = 160 + (((world_x * focal) >> 12) * inv_z >> RECIP_SH);
    screen_y = 112 + (((world_y * focal) >> 12) * inv_z >> RECIP_SH);
}
```
This replaces two expensive 32-bit hardware divides per vertex with single-cycle multiplications and shifts.

### Scanline / Depth Slice Divide Hoisting
When multiple points share the same depth coordinate (such as horizontal spans or terrain slices):
```c
int rf = (FOCAL << 12) / zz;         /* Perform ONE divide for the entire scanline */
for (int x = x0; x <= x1; x++) {
    sx = 160 + (((wx[x] >> 8) * rf) >> 12);  /* Per-pixel: multiply + shift */
}
```

### Eliminate Hidden 64-Bit Divides (`__divdi3`)
Writing `(dx << 16) / dy` where operands are `int32_t` causes GCC to promote the numerator to a 64-bit integer, generating a call to `__divdi3` (software 64-bit division).
- Replace with reciprocal table lookups:
  ```c
  int32_t inv_dy = div_recip_table[dy];
  int32_t slope = ((int64_t)dx * inv_dy) >> RECIP_SH;
  ```

---

## 4. The 60/n VBlank Quantization Model

Because `Mars_FlipFrameBuffers(1)` waits for VBlank, frame times on the 32X and Genesis are **strictly quantized to integer multiples of the VBlank period**:

$$\text{FPS} = \frac{60}{n} \quad (60, 30, 20, 15, 12, 10\dots)$$

- If a frame takes 1.01 VBlank intervals to render, the game drops immediately from **60 fps to 30 fps** (a 50% drop, not a 1% drop).
- If a frame takes 2.01 VBlank intervals, the game drops from **30 fps to 20 fps**.
- **Practical Implication:** Optimization is about getting below the next VBlank boundary. A micro-optimization that saves 5% of CPU time will show zero difference on an FPS counter if the frame does not cross a VBlank threshold, yet it provides crucial headroom against worst-case drops.

### Measuring Sub-VBlank Slack with Headroom Ballast
Because FPS is quantized, implement a **headroom probe** (`make headroom`):
- Inject calibrated dummy loop iterations (ballast) into the main loop.
- Measure how many ballast iterations the engine absorbs before dropping to the next VBlank step ($n \to n+1$).
- This provides a continuous metric to measure optimizations that do not cross step boundaries.

---

## 5. Dirty-Rectangle Rendering with Double Buffering

On the 32X, clearing and redrawing 320×224 pixels every frame consumes ~72 KiB of uncached writes. For scenes with static backgrounds (dungeon floors, UI panels, HUDs), use dirty-rectangle caching:

1. **Static Background Cache:** Pre-render static scenery (walls, floors, board) once into an offscreen SDRAM buffer `s_bg`.
2. **Double-Buffer Dirty Lists (`s_dirty[2]`):**
   - The 32X alternates between Framebuffer 0 and Framebuffer 1.
   - An entity rendered at $(x, y, w, h)$ on frame $N$ dirties Framebuffer 0.
   - On frame $N+1$, the entity is rendered to Framebuffer 1.
   - On frame $N+2$, Framebuffer 0 is displayed again: the old rectangle from frame $N$ must now be restored!
   - Maintain independent dirty lists per buffer:
     ```c
     typedef struct { int16_t x, y, w, h; } DirtyRect;
     static DirtyRect s_dirty[2][MAX_DIRTY];
     static int s_ndirty[2];
     ```
3. **Per-Frame Restoration Loop:**
   ```c
   int buf_idx = current_buffer_index;
   // 1. Restore pixels from cached background for all rects dirtied 2 frames ago
   for (int i = 0; i < s_ndirty[buf_idx]; i++) {
       restore_rect_from_bg(s_dirty[buf_idx][i]);
   }
   s_ndirty[buf_idx] = 0;

   // 2. Draw moving actors and record their new rectangles
   for (int i = 0; i < num_actors; i++) {
       draw_actor(&actors[i]);
       record_dirty_rect(buf_idx, actors[i].x, actors[i].y, actors[i].w, actors[i].h);
   }
   ```
   Reduces steady-state pixel writes from ~72,000 to <5,000 per frame (a 14× reduction).

---

## 6. Rasterization Optimization Checklist (Proven in Production)

1. **Precompute Polygon Edge Slopes:**
   - In polygon/quad rasterizers, never compute `dx/dy` per scanline using division. Compute edge slopes once per primitive in 16.16 fixed point and step scanlines with addition.
2. **Scanline Shared-Edge Strips:**
   - When rasterizing contiguous terrain or road segments, share boundary vertices so walking edges requires one slope calculation instead of two separate triangles.
3. **Span Function Inlining:**
   - In software renderers drawing thousands of horizontal spans per frame, function call overhead, parameter passing, and re-clipping consume >50% of CPU time. Inline horizontal span drawers directly into the rasterizer.
4. **4-Pixel-Aligned Memory Writes:**
   - Ensure horizontal span widths and starting coordinates align to 4-byte boundaries. Blit spans using 32-bit writes (`uint32_t`) rather than byte-by-byte loops.
5. **Hardware Line Doubling (`HALF_HEIGHT`):**
   - Render 112 lines of height instead of 224 lines. Point pairs of display lines in the Mars VDP line table to the same buffer row. The hardware performs line doubling for free, saving 50% of software rendering cycles.

---

## 7. Recommended Compiler Flags

For Release Builds on 32X (SH-2) and Genesis (68000):
```makefile
CFLAGS_RELEASE := -Os -flto -fuse-linker-plugin -fomit-frame-pointer \
                  -ffunction-sections -fdata-sections -fno-common \
                  -ffast-math -funroll-loops -fno-align-loops -fno-align-jumps \
                  -fno-align-labels
LDFLAGS_RELEASE := -Wl,--gc-sections
```
*Exception:* Always compile timing-critical modules (PWM audio mixer, Z80 bus drivers, hardware handshake loops) separately with:
```makefile
CFLAGS_AUDIO := -O2 -fno-lto
```
to ensure linker optimizations do not alter delay cycles or loop timing.
