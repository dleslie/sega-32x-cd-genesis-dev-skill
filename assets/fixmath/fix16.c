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

#include "fix16.h"

/* Addition and subtraction with overflow detection */
fix16_t fix16_add(fix16_t a, fix16_t b) {
    uint32_t _a = (uint32_t)a;
    uint32_t _b = (uint32_t)b;
    uint32_t sum = _a + _b;

    /* Overflow occurs if signs of a and b match, but sign of sum differs */
    if (!((_a ^ _b) & 0x80000000) && ((_a ^ sum) & 0x80000000)) {
        return fix16_overflow;
    }
    return (fix16_t)sum;
}

fix16_t fix16_sub(fix16_t a, fix16_t b) {
    uint32_t _a = (uint32_t)a;
    uint32_t _b = (uint32_t)b;
    uint32_t diff = _a - _b;

    /* Overflow occurs if signs of a and b differ, but sign of diff differs from a */
    if (((_a ^ _b) & 0x80000000) && ((_a ^ diff) & 0x80000000)) {
        return fix16_overflow;
    }
    return (fix16_t)diff;
}

/* Saturating addition and subtraction */
fix16_t fix16_sadd(fix16_t a, fix16_t b) {
    fix16_t result = fix16_add(a, b);
    if (result == fix16_overflow) {
        return (a >= 0) ? fix16_maximum : fix16_minimum;
    }
    return result;
}

fix16_t fix16_ssub(fix16_t a, fix16_t b) {
    fix16_t result = fix16_sub(a, b);
    if (result == fix16_overflow) {
        return (a >= 0) ? fix16_maximum : fix16_minimum;
    }
    return result;
}

/* 64-bit multiplication for Q16.16 */
fix16_t fix16_mul(fix16_t inArg0, fix16_t inArg1) {
    int64_t product = (int64_t)inArg0 * (int64_t)inArg1;
    uint32_t upper = (uint32_t)(product >> 47);

    if (product < 0) {
        if (~upper) {
            return fix16_overflow;
        }
#ifndef FIXMATH_NO_ROUNDING
        product--;
#endif
    } else {
        if (upper) {
            return fix16_overflow;
        }
    }

#ifdef FIXMATH_NO_ROUNDING
    return (fix16_t)(product >> 16);
#else
    fix16_t result = (fix16_t)(product >> 16);
    result += (fix16_t)((product & 0x8000) >> 15);
    return result;
#endif
}

fix16_t fix16_smul(fix16_t inArg0, fix16_t inArg1) {
    fix16_t result = fix16_mul(inArg0, inArg1);
    if (result == fix16_overflow) {
        if ((inArg0 >= 0) == (inArg1 >= 0)) {
            return fix16_maximum;
        } else {
            return fix16_minimum;
        }
    }
    return result;
}

/* Restoring binary division algorithm: optimal for processors without 32-bit hardware divider */
fix16_t fix16_div(fix16_t a, fix16_t b) {
    if (b == 0) {
        return fix16_minimum;
    }

    uint32_t remainder = (uint32_t)fix16_abs(a);
    uint32_t divider = (uint32_t)fix16_abs(b);
    uint32_t quotient = 0;
    uint32_t bit = 0x10000;

    /* Normalize divider */
    while (divider < remainder) {
        divider <<= 1;
        bit <<= 1;
    }

    if (!bit) {
        return fix16_overflow;
    }

    if (divider & 0x80000000) {
        if (remainder >= divider) {
            quotient |= bit;
            remainder -= divider;
        }
        divider >>= 1;
        bit >>= 1;
    }

    while (bit && remainder) {
        if (remainder >= divider) {
            quotient |= bit;
            remainder -= divider;
        }
        remainder <<= 1;
        bit >>= 1;
    }

#ifndef FIXMATH_NO_ROUNDING
    if (remainder >= divider) {
        quotient++;
    }
#endif

    fix16_t result = (fix16_t)quotient;
    if ((a ^ b) & 0x80000000) {
        if (result == fix16_minimum) {
            return fix16_overflow;
        }
        result = -result;
    }

    return result;
}

fix16_t fix16_sdiv(fix16_t inArg0, fix16_t inArg1) {
    fix16_t result = fix16_div(inArg0, inArg1);
    if (result == fix16_overflow) {
        if ((inArg0 >= 0) == (inArg1 >= 0)) {
            return fix16_maximum;
        } else {
            return fix16_minimum;
        }
    }
    return result;
}

fix16_t fix16_mod(fix16_t x, fix16_t y) {
    while (x >= y) x -= y;
    while (x <= -y) x += y;
    return x;
}

fix16_t fix16_lerp16(fix16_t inArg0, fix16_t inArg1, uint16_t inFract) {
    int64_t tempOut = (int64_t)inArg0 * (int64_t)(((int32_t)1 << 16) - inFract);
    tempOut += (int64_t)inArg1 * (int64_t)inFract;
    tempOut >>= 16;
    return (fix16_t)tempOut;
}
