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

static fix16_t fix16_sin_parabola(fix16_t inAngle) {
    fix16_t abs_inAngle, retval;
    fix16_t mask;
    fix16_t abs_retval;

    /* Absolute value without branching */
    mask = (inAngle >> 31);
    abs_inAngle = (inAngle + mask) ^ mask;

    /* Parabolic curve: 4/PI * x - 4/PI² * x² */
    retval = fix16_mul(FOUR_DIV_PI, inAngle) +
             fix16_mul(fix16_mul(_FOUR_DIV_PI2, inAngle), abs_inAngle);

    /* 4th-order polynomial correction */
    mask = (retval >> 31);
    abs_retval = (retval + mask) ^ mask;
    retval += fix16_mul(X4_CORRECTION_COMPONENT,
                       fix16_mul(retval, abs_retval) - retval);

    return retval;
}

fix16_t fix16_sin(fix16_t inAngle) {
    fix16_t tempAngle = inAngle % (fix16_pi << 1);

    if (tempAngle < 0) {
        tempAngle += (fix16_pi << 1);
    }
    if (tempAngle > fix16_pi) {
        tempAngle -= (fix16_pi << 1);
    }

    return fix16_sin_parabola(tempAngle);
}

fix16_t fix16_cos(fix16_t inAngle) {
    return fix16_sin(inAngle + (fix16_pi >> 1));
}

fix16_t fix16_tan(fix16_t inAngle) {
    return fix16_sdiv(fix16_sin(inAngle), fix16_cos(inAngle));
}

fix16_t fix16_atan2(fix16_t inY, fix16_t inX) {
    fix16_t abs_inY, mask, angle, r, r_3;

    if (inX == 0 && inY == 0) {
        return 0;
    }

    mask = (inY >> 31);
    abs_inY = (inY + mask) ^ mask;

    if (inX >= 0) {
        r = fix16_div((inX - abs_inY), (inX + abs_inY));
        r_3 = fix16_mul(fix16_mul(r, r), r);
        angle = fix16_mul(0x00003240, r_3) - fix16_mul(0x0000FB50, r) + PI_DIV_4;
    } else {
        r = fix16_div((inX + abs_inY), (abs_inY - inX));
        r_3 = fix16_mul(fix16_mul(r, r), r);
        angle = fix16_mul(0x00003240, r_3) - fix16_mul(0x0000FB50, r) + THREE_PI_DIV_4;
    }

    if (inY < 0) {
        angle = -angle;
    }

    return angle;
}

fix16_t fix16_atan(fix16_t x) {
    return fix16_atan2(x, fix16_one);
}
