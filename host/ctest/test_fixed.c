/*
 * HALDANE - host unit tests
 * Copyright (c) 2026 Faysal Ali Shah - MIT License
 *
 * test_fixed.c - fixed-point helpers.
 */
#include "ht.h"
#include "hd_fixed.h"

static uint32_t lcg_state = 12345u;

static uint32_t lcg(void)
{
    lcg_state = lcg_state * 1664525u + 1013904223u;
    return lcg_state;
}

static void test_shr_round(void)
{
    HT_CHECK_EQ(hd_shr32_round(5, 1), 3);       /* 2.5 -> 3  */
    HT_CHECK_EQ(hd_shr32_round(-5, 1), -3);     /* -2.5 -> -3 */
    HT_CHECK_EQ(hd_shr32_round(4, 1), 2);
    HT_CHECK_EQ(hd_shr32_round(-4, 1), -2);
    HT_CHECK_EQ(hd_shr32_round(7, 0), 7);
    HT_CHECK_EQ(hd_shr32_round(INT32_MIN, 31), -1);
    HT_CHECK_EQ(hd_shr64_round(-((int64_t)3 << 40), 40), -3);
    HT_CHECK_EQ(hd_shr64_round(((int64_t)3 << 40) + ((int64_t)1 << 39), 40), 4);
}

static void test_mul_shr(void)
{
    HT_CHECK_EQ(hd_mul_shr(3 << 16, 5 << 16, 16), 15 << 16);
    HT_CHECK_EQ(hd_mul_shr(-(3 << 16), 5 << 16, 16), -(15 << 16));
    HT_CHECK_EQ(hd_mul_shr(INT32_MAX, INT32_MAX, 0), INT32_MAX);   /* saturates */
    HT_CHECK_EQ(hd_mul_shr(INT32_MIN, INT32_MAX, 0), INT32_MIN);
    HT_CHECK_EQ(hd_sat_add32(INT32_MAX, 1), INT32_MAX);
    HT_CHECK_EQ(hd_sat_add32(INT32_MIN, -1), INT32_MIN);
    HT_CHECK_EQ(hd_clamp32(9, 0, 5), 5);
    HT_CHECK_EQ(hd_clamp32(-9, 0, 5), 0);
}

static void test_udiv_frac_matches_64bit(void)
{
    int i;
    int bad = 0;
    for (i = 0; i < 20000; ++i) {
        uint32_t den = (lcg() >> 1) | 1u;             /* < 2^31, non-zero */
        uint32_t num = lcg() % (den * 4u + 1u);
        uint8_t f = (uint8_t)(lcg() % 29u);
        uint64_t ref = ((uint64_t)num << f) / den;
        uint32_t got = hd_udiv_frac(num, den, f);
        if (ref > UINT32_MAX) {
            if (got != UINT32_MAX) {
                ++bad;
            }
        } else if ((uint64_t)got != ref) {
            ++bad;
        }
    }
    HT_CHECK_EQ(bad, 0);
}

static void test_udiv_frac_edges(void)
{
    HT_CHECK_EQ(hd_udiv_frac(1u, 0u, 8), UINT32_MAX);
    HT_CHECK_EQ(hd_udiv_frac(1u, 0x80000000u, 8), UINT32_MAX);
    HT_CHECK_EQ(hd_udiv_frac(1u, 3u, 0), 0u);
    HT_CHECK_EQ(hd_udiv_frac(1u, 2u, 30), 1u << 29);
    HT_CHECK_EQ(hd_udiv_frac(70000u, 1u, 16), UINT32_MAX);      /* overflow */
}

static void test_log2_accuracy(void)
{
    double worst = 0.0;
    double v;
    for (v = 0.001; v < 30000.0; v *= 1.013) {
        uint32_t x = (uint32_t)(v * 65536.0 + 0.5);
        double ref = log2((double)x / 65536.0);
        double got = hd_log2_q16(x) / 65536.0;
        double err = fabs(got - ref);
        if (err > worst) {
            worst = err;
        }
    }
    HT_CHECK(worst < 2.5e-4);
    HT_CHECK_EQ(hd_log2_q16(65536u), 0);
    HT_CHECK_EQ(hd_log2_q16(131072u), 65536);
    HT_CHECK_EQ(hd_log2_q16(0u), INT32_MIN);
}

static void test_pow2_accuracy(void)
{
    double worst = 0.0;
    double y;
    for (y = -10.0; y < 31.5; y += 0.0137) {
        int32_t yq = (int32_t)floor(y * 65536.0 + 0.5);
        double ref = pow(2.0, yq / 65536.0);
        uint32_t got = hd_pow2_q16(yq);
        double rel;
        if (ref < 64.0) {
            continue;
        }
        /* allow the final integer rounding (0.5) on top of the table error */
        rel = (fabs((double)got - ref) - 0.5) / ref;
        if (rel > worst) {
            worst = rel;
        }
    }
    HT_CHECK(worst < 2.5e-4);
    HT_CHECK_EQ(hd_pow2_q16(0), 1u);
    HT_CHECK_EQ(hd_pow2_q16(10 * 65536), 1024u);
    HT_CHECK_EQ(hd_pow2_q16(33 * 65536), UINT32_MAX);
    HT_CHECK_EQ(hd_pow2_q16(-40 * 65536), 0u);
}

static void test_ewma(void)
{
    int32_t acc = 0;
    int i;
    for (i = 0; i < 400; ++i) {
        acc = hd_ewma_q8(acc, 10000, 30);
    }
    HT_CHECK_NEAR(acc, 10000, 5);
    acc = 0;
    for (i = 0; i < 400; ++i) {
        acc = hd_ewma_shift(acc, -8000, 5);
    }
    HT_CHECK_NEAR(acc, -8000, 16);
}

void test_fixed_all(void)
{
    ht_run("fixed.shr_round", test_shr_round);
    ht_run("fixed.mul_shr", test_mul_shr);
    ht_run("fixed.udiv_frac_vs_64bit", test_udiv_frac_matches_64bit);
    ht_run("fixed.udiv_frac_edges", test_udiv_frac_edges);
    ht_run("fixed.log2_accuracy", test_log2_accuracy);
    ht_run("fixed.pow2_accuracy", test_pow2_accuracy);
    ht_run("fixed.ewma", test_ewma);
}
