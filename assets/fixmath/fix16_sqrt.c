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

/* Fast integer square root using only 32-bit operations.
 * Returns -sqrt(-inValue) for negative inputs.
 */
fix16_t fix16_sqrt(fix16_t inValue) {
    uint8_t neg = (inValue < 0);
    uint32_t num = (uint32_t)fix16_abs(inValue);
    uint32_t result = 0;
    uint32_t bit;
    uint8_t n;

    if (num & 0xFFF00000) {
        bit = (uint32_t)1 << 30;
    } else {
        bit = (uint32_t)1 << 18;
    }

    while (bit > num) {
        bit >>= 2;
    }

    /* Main algorithm executed in two 8-iteration stages for precision */
    for (n = 0; n < 2; n++) {
        while (bit > 0) {
            if (num >= result + bit) {
                num -= result + bit;
                result = (result >> 1) + bit;
            } else {
                result = (result >> 1);
            }
            bit >>= 2;
        }

        if (n == 0) {
            if (num > 65535) {
                num -= result;
                num = (num << 16) - 0x8000;
                result = (result << 16) + 0x8000;
            } else {
                num <<= 16;
                result <<= 16;
            }
            bit = 1 << 14;
        }
    }

#ifndef FIXMATH_NO_ROUNDING
    if (num > result) {
        result++;
    }
#endif

    return neg ? -(fix16_t)result : (fix16_t)result;
}
