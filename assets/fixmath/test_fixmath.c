/*
 * test_fixmath.c - Host unit tests for minimal libfixmath implementation.
 *
 * Verifies basic arithmetic, saturating arithmetic, square root,
 * and trigonometry against floating point reference values.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include "fix16.h"

#define EPSILON_FLOAT 0.005f

static void test_conversions(void) {
    printf("Testing conversions...\n");
    assert(fix16_from_int(1) == fix16_one);
    assert(fix16_from_int(-1) == -fix16_one);
    assert(fix16_to_int(fix16_one) == 1);
    assert(fix16_to_int(-fix16_one) == -1);

    fix16_t f1 = fix16_from_float(1.5f);
    assert(fix16_to_int(f1) == 2); /* rounded */
    assert(fabsf(fix16_to_float(f1) - 1.5f) < 0.0001f);

    fix16_t f2 = fix16_from_float(-2.75f);
    assert(fabsf(fix16_to_float(f2) - -2.75f) < 0.0001f);
}

static void test_arithmetic(void) {
    printf("Testing arithmetic...\n");
    fix16_t a = fix16_from_int(5);
    fix16_t b = fix16_from_int(3);

    assert(fix16_add(a, b) == fix16_from_int(8));
    assert(fix16_sub(a, b) == fix16_from_int(2));
    assert(fix16_mul(a, b) == fix16_from_int(15));
    assert(fix16_div(a, b) == fix16_from_float(5.0f / 3.0f));

    /* Negative numbers */
    fix16_t neg_a = fix16_from_int(-5);
    assert(fix16_mul(neg_a, b) == fix16_from_int(-15));
    assert(fix16_mul(neg_a, -b) == fix16_from_int(15));

    /* Saturating arithmetic */
    assert(fix16_sadd(fix16_maximum, fix16_one) == fix16_maximum);
    assert(fix16_ssub(fix16_minimum, fix16_one) == fix16_minimum);
}

static void test_sqrt(void) {
    printf("Testing square root...\n");
    assert(fix16_sqrt(fix16_from_int(0)) == 0);
    assert(fix16_sqrt(fix16_from_int(1)) == fix16_one);
    assert(fix16_sqrt(fix16_from_int(4)) == fix16_from_int(2));
    assert(fix16_sqrt(fix16_from_int(9)) == fix16_from_int(3));
    assert(fix16_sqrt(fix16_from_int(16)) == fix16_from_int(4));
    assert(fix16_sqrt(fix16_from_int(25)) == fix16_from_int(5));

    fix16_t s2 = fix16_sqrt(fix16_from_int(2));
    assert(fabsf(fix16_to_float(s2) - 1.414213f) < 0.001f);
}

static void test_trig(void) {
    printf("Testing trigonometry...\n");
    /* Sin */
    assert(fix16_abs(fix16_sin(0)) < fix16_from_float(0.001f));
    assert(fabsf(fix16_to_float(fix16_sin(fix16_pi / 2)) - 1.0f) < EPSILON_FLOAT);
    assert(fabsf(fix16_to_float(fix16_sin(fix16_pi)) - 0.0f) < EPSILON_FLOAT);
    assert(fabsf(fix16_to_float(fix16_sin(-fix16_pi / 2)) - -1.0f) < EPSILON_FLOAT);

    /* Cos */
    assert(fabsf(fix16_to_float(fix16_cos(0)) - 1.0f) < EPSILON_FLOAT);
    assert(fabsf(fix16_to_float(fix16_cos(fix16_pi / 2)) - 0.0f) < EPSILON_FLOAT);
    assert(fabsf(fix16_to_float(fix16_cos(fix16_pi)) - -1.0f) < EPSILON_FLOAT);

    /* Atan2 */
    assert(fix16_atan2(0, 0) == 0);
    fix16_t a1 = fix16_atan2(fix16_one, fix16_one);
    assert(fabsf(fix16_to_float(a1) - (3.14159265f / 4.0f)) < EPSILON_FLOAT);

    fix16_t a2 = fix16_atan2(0, fix16_one);
    assert(fabsf(fix16_to_float(a2) - 0.0f) < EPSILON_FLOAT);
}

int main(void) {
    printf("=== Running Minimal libfixmath Tests ===\n");
    test_conversions();
    test_arithmetic();
    test_sqrt();
    test_trig();
    printf("ALL TESTS PASSED!\n");
    return 0;
}
