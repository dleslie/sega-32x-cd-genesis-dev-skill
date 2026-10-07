/*
 * libfixmath - Cross-platform Q16.16 fixed-point math library
 *
 * Copyright (c) 2011-2021 Flatmush <Flatmush@gmail.com>,
 *                         Petteri Aimonen <Petteri.Aimonen@gmail.com>,
 *                         and libfixmath AUTHORS (Chris Hammond, David Lechner,
 *                         Gaëtan Harter, Joe Schaack, Martin Larralde, Stargirl Flowers,
 *                         Vincent del Medico, Vitaly Puzrin, Xin Li, J.P. Hutchins).
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __libfixmath_fix16_h__
#define __libfixmath_fix16_h__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef FIXMATH_FUNC_ATTRS
# ifdef __GNUC__
#   define FIXMATH_FUNC_ATTRS __attribute__((nothrow, const))
# else
#   define FIXMATH_FUNC_ATTRS
# endif
#endif

typedef int32_t fix16_t;

/* Constants */
static const fix16_t FOUR_DIV_PI            = 0x145F3;     /*!< Fix16 value of 4/PI (1.27323954) */
static const fix16_t _FOUR_DIV_PI2           = 0xFFFF9840;  /*!< Fix16 value of -4/PI² (-0.40528473) */
static const fix16_t X4_CORRECTION_COMPONENT = 0x399A;      /*!< Fix16 value of 0.225 */
static const fix16_t PI_DIV_4                = 0x0000C90F;  /*!< Fix16 value of PI/4 (0.78539816) */
static const fix16_t THREE_PI_DIV_4          = 0x00025B2F;  /*!< Fix16 value of 3PI/4 (2.35619449) */

static const fix16_t fix16_maximum           = 0x7FFFFFFF;  /*!< Maximum value: 32767.9999847 */
static const fix16_t fix16_minimum           = 0x80000000;  /*!< Minimum value: -32768.0 */
static const fix16_t fix16_overflow          = 0x80000000;  /*!< Overflow marker */

static const fix16_t fix16_pi                = 205887;      /*!< Fix16 value of PI (3.14159265) */
static const fix16_t fix16_e                 = 178145;      /*!< Fix16 value of e (2.71828183) */
static const fix16_t fix16_one               = 0x00010000;  /*!< Fix16 value of 1.0 */
static const fix16_t fix16_eps               = 1;           /*!< Fix16 epsilon (0.000015258789) */

/* Macro to convert literal float constant at compile time */
#define F16(x) ((fix16_t)(((x) >= 0.0f) ? ((x) * 65536.0f + 0.5f) : ((x) * 65536.0f - 0.5f)))

/* Conversions */
static inline fix16_t fix16_from_int(int a) {
    return (fix16_t)a * fix16_one;
}

static inline float fix16_to_float(fix16_t a) {
    return (float)a / (float)fix16_one;
}

static inline double fix16_to_dbl(fix16_t a) {
    return (double)a / (double)fix16_one;
}

static inline int fix16_to_int(fix16_t a) {
#ifdef FIXMATH_NO_ROUNDING
    return (a >> 16);
#else
    if (a >= 0) {
        return (a + (fix16_one >> 1)) >> 16;
    } else {
        return -((-a + (fix16_one >> 1)) >> 16);
    }
#endif
}

static inline fix16_t fix16_from_float(float a) {
    float temp = a * 65536.0f;
#ifndef FIXMATH_NO_ROUNDING
    temp += (temp >= 0.0f) ? 0.5f : -0.5f;
#endif
    return (fix16_t)temp;
}

static inline fix16_t fix16_from_dbl(double a) {
    double temp = a * 65536.0;
#ifndef FIXMATH_NO_ROUNDING
    temp += (temp >= 0.0) ? 0.5 : -0.5;
#endif
    return (fix16_t)temp;
}

/* Common utility functions */
static inline fix16_t fix16_abs(fix16_t x) {
    return (x < 0) ? -x : x;
}

static inline fix16_t fix16_floor(fix16_t x) {
    return (x & 0xFFFF0000);
}

static inline fix16_t fix16_ceil(fix16_t x) {
    return (x & 0x0000FFFF) ? (fix16_t)((x & (uint32_t)0xFFFF0000) + fix16_one) : x;
}

static inline fix16_t fix16_min(fix16_t a, fix16_t b) {
    return (a < b) ? a : b;
}

static inline fix16_t fix16_max(fix16_t a, fix16_t b) {
    return (a > b) ? a : b;
}

static inline fix16_t fix16_clamp(fix16_t x, fix16_t lo, fix16_t hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

/* Fast SH-2 hardware-accelerated Q16.16 multiply (32X)
 * Executes dmuls.l + xtrct in 4 cycles on Hitachi SH-2!
 */
#if (defined(__SH2__) || defined(__sh__)) && !defined(FIXMATH_NO_ASM)
static inline fix16_t fix16_fast_mul_sh2(fix16_t inArg0, fix16_t inArg1) {
    register fix16_t res;
    __asm__ (
        "dmuls.l %1, %2\n\t"
        "sts     mach, r0\n\t"
        "sts     macl, %0\n\t"
        "xtrct   r0, %0"
        : "=r" (res)
        : "r" (inArg0), "r" (inArg1)
        : "r0", "mach", "macl"
    );
    return res;
}
#endif

/* Arithmetic functions */
extern fix16_t fix16_add(fix16_t a, fix16_t b) FIXMATH_FUNC_ATTRS;
extern fix16_t fix16_sub(fix16_t a, fix16_t b) FIXMATH_FUNC_ATTRS;
extern fix16_t fix16_sadd(fix16_t a, fix16_t b) FIXMATH_FUNC_ATTRS;
extern fix16_t fix16_ssub(fix16_t a, fix16_t b) FIXMATH_FUNC_ATTRS;

extern fix16_t fix16_mul(fix16_t inArg0, fix16_t inArg1) FIXMATH_FUNC_ATTRS;
extern fix16_t fix16_smul(fix16_t inArg0, fix16_t inArg1) FIXMATH_FUNC_ATTRS;

extern fix16_t fix16_div(fix16_t a, fix16_t b) FIXMATH_FUNC_ATTRS;
extern fix16_t fix16_sdiv(fix16_t inArg0, fix16_t inArg1) FIXMATH_FUNC_ATTRS;

extern fix16_t fix16_mod(fix16_t x, fix16_t y) FIXMATH_FUNC_ATTRS;
extern fix16_t fix16_lerp16(fix16_t inArg0, fix16_t inArg1, uint16_t inFract) FIXMATH_FUNC_ATTRS;

/* Square root */
extern fix16_t fix16_sqrt(fix16_t inValue) FIXMATH_FUNC_ATTRS;

/* Trigonometry (Parabolic polynomial approximation, zero large LUT RAM footprint) */
extern fix16_t fix16_sin(fix16_t inAngle) FIXMATH_FUNC_ATTRS;
extern fix16_t fix16_cos(fix16_t inAngle) FIXMATH_FUNC_ATTRS;
extern fix16_t fix16_tan(fix16_t inAngle) FIXMATH_FUNC_ATTRS;
extern fix16_t fix16_atan(fix16_t x) FIXMATH_FUNC_ATTRS;
extern fix16_t fix16_atan2(fix16_t inY, fix16_t inX) FIXMATH_FUNC_ATTRS;

#ifdef __cplusplus
}
#endif

#endif /* __libfixmath_fix16_h__ */
