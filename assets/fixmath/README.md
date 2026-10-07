# Minimal libfixmath for Sega Mega Drive / Genesis, Sega CD, and 32X

A lightweight, self-contained subset of **libfixmath** (Q16.16 fixed-point math) tailored for 16-bit and 32-bit Sega consoles.

## Why Fixed-Point Math?

Neither the **Motorola 68000** (Genesis / Sega CD Main CPU / Sega CD Sub-CPU), the **Zilog Z80** (sound coprocessor), nor the **Hitachi SH-2** (Sega 32X dual CPUs) possess a hardware Floating-Point Unit (FPU).

### Hardware Cost Model: Float vs Fixed-Point

| Operation | Motorola 68000 (Genesis @ 7.67 MHz) | Zilog Z80 (@ 3.58 MHz) | Hitachi SH-2 (32X @ 23 MHz) |
|---|---|---|---|
| **Float Add (`__addsf3`)** | ~120–250 cycles (software) | ~1,500–2,500 cycles (software) | ~60–100 cycles (software) |
| **Float Mul (`__mulsf3`)** | ~300–800 cycles (software) | ~3,000–5,000 cycles (software) | ~80–150 cycles (software) |
| **Float Div (`__divsf3`)** | > 1,000 cycles (software) | > 6,000 cycles (software) | > 200 cycles (software) |
| **Fixed Q16.16 Add (`fix16_add`)** | **8 cycles** (`add.l d0, d1`) | **11 cycles** (`add hl, de`) | **1 cycle** (`add rm, rn`) |
| **Fixed Q16.16 Mul (`fix16_mul`)** | **~100 cycles** (split `muls.w`) | ~400 cycles (integer loop) | **4 cycles** (`dmuls.l` + `xtrct`!) |
| **Fixed 8.8 Mul** | **~78 cycles** (single `muls.w` + shift)| ~150 cycles | **2 cycles** (`muls.w` / shift) |

### SH-2 Hardware MAC Acceleration (`dmuls.l` + `xtrct`)

The Hitachi SH-2 processor features a 32×32 $\to$ 64-bit hardware integer multiplier executing in 2–4 cycles. `fix16.h` provides an inline assembly helper that executes a complete Q16.16 multiplication in only **4 cycles**:

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

## Features Included in This Minimal Distribution

- **Q16.16 Type & Constants**: `fix16_t`, `fix16_one` (`0x10000`), `fix16_pi`, `fix16_e`, `fix16_maximum`, `fix16_minimum`, `fix16_overflow`, `F16(float_lit)`.
- **Fast Conversions**: `fix16_from_int`, `fix16_to_int`, `fix16_from_float`, `fix16_to_float`, `fix16_from_dbl`, `fix16_to_dbl`.
- **Core Arithmetic**: `fix16_add`, `fix16_sub`, `fix16_sadd`, `fix16_ssub`, `fix16_mul`, `fix16_smul`, `fix16_div`, `fix16_sdiv`, `fix16_mod`, `fix16_lerp16`.
- **Square Root**: `fix16_sqrt` (pure 32-bit integer binary restoring square root).
- **Trigonometry**: `fix16_sin`, `fix16_cos`, `fix16_tan`, `fix16_atan`, `fix16_atan2`. Uses a 4th-order polynomial parabolic curve with zero RAM cache/table allocation (critical for memory-constrained 68000 Work RAM and SH-2 SDRAM).

## License & Attribution

This code is adapted from **libfixmath**:
- **Repository**: https://github.com/PetteriAimonen/libfixmath
- **Copyright**:
  - Copyright (c) 2011-2021 Flatmush `<Flatmush@gmail.com>`
  - Petteri Aimonen `<Petteri.Aimonen@gmail.com>`
  - libfixmath AUTHORS: Chris Hammond, David Lechner, Gaëtan Harter, Joe Schaack, Martin Larralde, Stargirl Flowers, Vincent del Medico, Vitaly Puzrin, Xin Li, J.P. Hutchins.
- **License**: MIT License (Expat). See header files and root `LICENSE` for complete text.
