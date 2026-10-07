# Fixed-Point Math & libfixmath Reference Guide

## 1. Hardware Reality: Why Fixed-Point is Mandatory

None of the primary processors in the Sega 16-bit / 32-bit ecosystem contain a hardware Floating-Point Unit (FPU):
- **Motorola 68000** (Genesis Main-CPU @ 7.67 MHz, Sega CD Main-CPU @ 7.67 MHz, Sega CD Sub-CPU @ 12.5 MHz).
- **Zilog Z80** (Genesis sound co-processor @ 3.58 MHz).
- **Hitachi SH-2** (Sega 32X dual Master/Slave RISC CPUs @ 23.01 MHz).

Calling standard floating-point functions (`float`, `double`, `sinf`, `sqrtf`, etc.) triggers software floating-point emulation in GCC runtime libraries (`libgcc` helpers: `__addsf3`, `__mulsf3`, `__divsf3`). These routines consume hundreds to thousands of clock cycles per single operation.

### Clock Cycle Comparison: Floating-Point vs Fixed-Point

| Architecture | CPU Clock | Operation | Software IEEE-754 Float | Fixed-Point (Q16.16) | Speedup Factor |
|---|---|---|---|---|---|
| **Motorola 68000** | 7.67 MHz | **Add / Sub** | ~120–250 cycles | **8 cycles** (`add.l d0, d1`) | **~20×** |
| | | **Multiply** | ~300–800 cycles | **~100 cycles** (`muls.w` split) | **~5×** |
| | | **Divide** | > 1,000 cycles | **~160 cycles** (restoring 16-bit) | **~6×** |
| **Zilog Z80** | 3.58 MHz | **Add / Sub** | ~1,500–2,500 cycles | **11 cycles** (`add hl, de`) | **~150×** |
| | | **Multiply** | ~3,000–5,000 cycles | **~150–400 cycles** | **~15×** |
| **Hitachi SH-2** | 23.01 MHz | **Add / Sub** | ~60–100 cycles | **1 cycle** (`add rm, rn`) | **~80×** |
| | | **Multiply** | ~80–150 cycles | **4 cycles** (`dmuls.l` + `xtrct`) | **~25×** |
| | | **Divide** | > 200 cycles | **~35 cycles** (restoring / reciprocal) | **~6×** |

On a 60 fps NTSC frame, each frame provides approximately:
- Genesis 68000: **128,000 CPU cycles**.
- Sega 32X SH-2: **383,000 CPU cycles** per core.
- Z80: **59,700 CPU cycles**.

A single 3D transformation loop doing 100 vertex multiplies in software float will consume over 80,000 cycles on 68000 (over 60% of the entire frame budget!), causing immediate sub-20 fps drops. Using fixed-point math reduces this to under 8,000 cycles.

---

## 2. Minimal libfixmath Architecture

The skill includes a minimal, zero-dependency distribution of **libfixmath** located in `assets/fixmath/`.

### Format: Q16.16
- Represented as a signed 32-bit integer: `typedef int32_t fix16_t;`
- High 16 bits: Integer portion (range: `-32768` to `+32767`).
- Low 16 bits: Fractional portion ($1 / 65536 \approx 0.00001526$).
- `fix16_one = 0x00010000` (65536).

### Compile-Time Literal Macro: `F16(val)`
Use `F16(...)` for static constant initialization:
```c
static const fix16_t SPEED = F16(2.75);
static const fix16_t GRAVITY = F16(0.125);
```

### Conversions
```c
fix16_t a = fix16_from_int(10);        /* 10 * 65536 */
int b = fix16_to_int(a);              /* Rounded integer conversion */
fix16_t c = fix16_from_float(3.14159f);/* Float to Q16.16 */
float d = fix16_to_float(c);          /* Q16.16 to float (host debug only) */
```

---

## 3. Hardware Optimizations by Architecture

### A. Hitachi SH-2 (Sega 32X): 4-Cycle Hardware MAC (`dmuls.l` + `xtrct`)
The SH-2 has a 32×32 $\to$ 64-bit hardware integer multiplier writing to `MACH:MACL`.
Using GCC's 64-bit multiply `(int64_t)a * b >> 16` can sometimes emit extra register moves or stack spills depending on optimization level.
The optimal assembly sequence extracts the middle 32 bits directly using the SH-2 `xtrct` instruction:

```c
static inline fix16_t fix16_fast_mul_sh2(fix16_t a, fix16_t b) {
    register fix16_t res;
    __asm__ (
        "dmuls.l %1, %2\n\t"
        "sts     mach, r0\n\t"
        "sts     macl, %0\n\t"
        "xtrct   r0, %0"
        : "=r" (res)
        : "r" (a), "r" (b)
        : "r0", "mach", "macl"
    );
    return res;
}
```
Total cost: **4 CPU cycles**.

### B. Motorola 68000 (Genesis & Sega CD)
The standard 68000 does not have a 32-bit hardware multiply instruction, only `muls.w` (16×16 signed $\to$ 32-bit, up to 70 cycles).
For micro-critical paths (e.g., raycasting or particle updates), prefer **Q8.8** fixed-point:
```c
/* Q8.8 in Motorola 68000: single muls.w instruction */
typedef int16_t fix8_t;
#define FIX8_ONE 256
static inline fix8_t fix8_mul(fix8_t a, fix8_t b) {
    return (fix8_t)(((int32_t)a * b) >> 8);
}
```

### C. Zilog Z80 (Genesis Sound Processor)
The Z80 has no multiply instruction. Frequency stepping and pitch modulation in custom sound drivers must use:
- **16-bit phase accumulators**: High byte represents current sample index, low byte represents sub-sample fractional phase.
- Step addition: `ADD HL, DE` (11 T-states / ~3.07 $\mu$s).

---

## 4. Trigonometry Without RAM Cache Footprint

Standard desktop math libraries use lookup tables with 4,096 entries (16–32 KiB of RAM).
On the Sega Genesis (64 KiB total Work RAM) or Sega 32X (256 KiB SDRAM), dedicating 32 KiB to a trig cache wastes 12–50% of available memory.

`assets/fixmath/fix16_trig.c` solves this using a **4th-order polynomial parabolic curve** (Bhaskara / Chebyshev approximation) with **zero RAM footprint**:
$$\text{approx}(x) = \frac{4}{\pi} x - \frac{4}{\pi^2} x |x|$$
$$\sin(x) \approx \text{approx}(x) + 0.225 \cdot (\text{approx}(x) \cdot |\text{approx}(x)| - \text{approx}(x))$$

- **Accuracy**: Max error $< 0.05\%$.
- **RAM Overhead**: 0 bytes.
- **Compute Time**: 4 fixed-point multiplies (~16 cycles on SH-2).

---

## 5. Integrating libfixmath in Your Project

### Sega 32X (Makefile.32x / MarsDev)
Copy `assets/fixmath/` into `src/core/math/`:
```make
MATH_OBJS = $(BUILD)/fix16.o $(BUILD)/fix16_sqrt.o $(BUILD)/fix16_trig.o
```

### Sega Genesis (SGDK)
SGDK has basic `fix16` and `fix32` types in `genesis.h`, but lacks saturating arithmetic, polynomial trigonometry, and fast arctangent. Include `assets/fixmath/` when higher precision or non-LUT trigonometry is required.

---

## 6. License Attribution

Adapted from **libfixmath**:
- **Authors**: Flatmush `<Flatmush@gmail.com>`, Petteri Aimonen `<Petteri.Aimonen@gmail.com>`, and libfixmath AUTHORS (Chris Hammond, David Lechner, Gaëtan Harter, Joe Schaack, Martin Larralde, Stargirl Flowers, Vincent del Medico, Vitaly Puzrin, Xin Li, J.P. Hutchins).
- **Repository**: https://github.com/PetteriAimonen/libfixmath
- **License**: MIT License (Expat).
