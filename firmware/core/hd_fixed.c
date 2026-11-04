/*
 * HALDANE - bare-metal CO + fire safety node
 * Copyright (c) 2026 Faysal Ali Shah - MIT License
 *
 * hd_fixed.c - fixed-point helpers (see hd_fixed.h).
 */
#include "hd_fixed.h"

/* log2(1 + i/32) in Q16, i = 0..32. */
static const HD_ROM uint32_t k_log2_tab[33] = {
    UINT32_C(0), UINT32_C(2909), UINT32_C(5732), UINT32_C(8473), UINT32_C(11136), UINT32_C(13727), UINT32_C(16248), UINT32_C(18704),
    UINT32_C(21098), UINT32_C(23433), UINT32_C(25711), UINT32_C(27936), UINT32_C(30109), UINT32_C(32234), UINT32_C(34312), UINT32_C(36346),
    UINT32_C(38336), UINT32_C(40286), UINT32_C(42196), UINT32_C(44068), UINT32_C(45904), UINT32_C(47705), UINT32_C(49472), UINT32_C(51207),
    UINT32_C(52911), UINT32_C(54584), UINT32_C(56229), UINT32_C(57845), UINT32_C(59434), UINT32_C(60997), UINT32_C(62534), UINT32_C(64047),
    UINT32_C(65536),
};

/* 2^(i/32) in Q15, i = 0..32. */
static const HD_ROM uint32_t k_exp2_tab[33] = {
    UINT32_C(32768), UINT32_C(33486), UINT32_C(34219), UINT32_C(34968), UINT32_C(35734), UINT32_C(36516), UINT32_C(37316), UINT32_C(38133),
    UINT32_C(38968), UINT32_C(39821), UINT32_C(40693), UINT32_C(41584), UINT32_C(42495), UINT32_C(43425), UINT32_C(44376), UINT32_C(45348),
    UINT32_C(46341), UINT32_C(47356), UINT32_C(48393), UINT32_C(49452), UINT32_C(50535), UINT32_C(51642), UINT32_C(52773), UINT32_C(53928),
    UINT32_C(55109), UINT32_C(56316), UINT32_C(57549), UINT32_C(58809), UINT32_C(60097), UINT32_C(61413), UINT32_C(62757), UINT32_C(64132),
    UINT32_C(65536),
};

int32_t hd_shr32_round(int32_t v, uint8_t s)
{
    uint32_t half;
    uint32_t mag;
    if (s == 0u) {
        return v;
    }
    if (s > 31u) {
        return 0;
    }
    half = UINT32_C(1) << (s - 1u);
    if (v >= 0) {
        mag = ((uint32_t)v + half) >> s;
        return (int32_t)mag;
    }
    mag = (uint32_t)(-(int64_t)v);
    mag = (mag + half) >> s;
    return -(int32_t)mag;
}

int64_t hd_shr64_round(int64_t v, uint8_t s)
{
    uint64_t half;
    uint64_t mag;
    if (s == 0u) {
        return v;
    }
    if (s > 62u) {
        return 0;
    }
    half = UINT64_C(1) << (s - 1u);
    if (v >= 0) {
        mag = ((uint64_t)v + half) >> s;
        return (int64_t)mag;
    }
    mag = (uint64_t)0 - (uint64_t)v;
    mag = (mag + half) >> s;
    return -(int64_t)mag;
}

int32_t hd_mul_shr(int32_t a, int32_t b, uint8_t s)
{
    int64_t r = hd_shr64_round((int64_t)a * (int64_t)b, s);
    if (r > INT32_MAX) {
        return INT32_MAX;
    }
    if (r < INT32_MIN) {
        return INT32_MIN;
    }
    return (int32_t)r;
}

int32_t hd_clamp32(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

int32_t hd_sat_add32(int32_t a, int32_t b)
{
    int64_t r = (int64_t)a + (int64_t)b;
    if (r > INT32_MAX) {
        return INT32_MAX;
    }
    if (r < INT32_MIN) {
        return INT32_MIN;
    }
    return (int32_t)r;
}

uint32_t hd_udiv_frac(uint32_t num, uint32_t den, uint8_t frac_bits)
{
    uint32_t q;
    uint32_t r;
    uint8_t i;

    if (den == 0u || den >= (UINT32_C(1) << 31) || frac_bits > 31u) {
        return UINT32_MAX;
    }
    q = num / den;
    r = num % den;
    if (frac_bits > 0u && q >= (UINT32_C(1) << (32u - frac_bits))) {
        return UINT32_MAX;
    }
    for (i = 0u; i < frac_bits; ++i) {
        r <<= 1;            /* r < den < 2^31, so this cannot overflow */
        q <<= 1;
        if (r >= den) {
            r -= den;
            q |= 1u;
        }
    }
    return q;
}

int32_t hd_log2_q16(uint32_t x_q16)
{
    uint8_t p = 31u;
    uint32_t m;
    uint32_t f;
    uint32_t idx;
    uint32_t frac;
    uint32_t lo;
    uint32_t hi;
    uint32_t interp;

    if (x_q16 == 0u) {
        return INT32_MIN;
    }
    while ((x_q16 & (UINT32_C(1) << p)) == 0u) {
        --p;
    }
    m = x_q16 << (31u - p);              /* bit 31 set: 1.f */
    f = (m >> 15) & UINT32_C(0xFFFF);    /* 16-bit fraction  */
    idx = f >> 11;
    frac = f & UINT32_C(0x7FF);
    lo = k_log2_tab[idx];
    hi = k_log2_tab[idx + 1u];
    interp = lo + (((hi - lo) * frac + UINT32_C(1024)) >> 11);
    return ((int32_t)p - INT32_C(16)) * HD_Q16_ONE + (int32_t)interp;
}

uint32_t hd_pow2_q16(int32_t y_q16)
{
    int32_t ip;
    uint32_t f;
    uint32_t idx;
    uint32_t frac;
    uint32_t lo;
    uint32_t hi;
    uint32_t m;
    int32_t sh;

    if (y_q16 >= INT32_C(32) * HD_Q16_ONE) {
        return UINT32_MAX;
    }
    if (y_q16 < INT32_C(-32) * HD_Q16_ONE) {
        return 0u;
    }
    if (y_q16 >= 0) {
        ip = y_q16 >> 16;
    } else {
        ip = -(int32_t)(((uint32_t)(-y_q16) + UINT32_C(0xFFFF)) >> 16);
    }
    f = (uint32_t)(y_q16 - ip * HD_Q16_ONE);   /* 0 .. 65535 */
    idx = f >> 11;
    frac = f & UINT32_C(0x7FF);
    lo = k_exp2_tab[idx];
    hi = k_exp2_tab[idx + 1u];
    m = lo + (((hi - lo) * frac + UINT32_C(1024)) >> 11);   /* Q15, 2^15..2^16 */

    sh = ip - INT32_C(15);
    if (sh >= 17) {
        return UINT32_MAX;
    }
    if (sh == 16) {
        return (m >= UINT32_C(65536)) ? UINT32_MAX : (m << 16);
    }
    if (sh >= 0) {
        return m << sh;
    }
    sh = -sh;
    if (sh > 18) {
        return 0u;
    }
    return (m + (UINT32_C(1) << (sh - 1))) >> sh;
}

int32_t hd_ewma_q8(int32_t acc, int32_t x, uint8_t alpha_q8)
{
    int32_t diff = x - acc;
    return acc + hd_shr32_round(diff * (int32_t)alpha_q8, 8u);
}

int32_t hd_ewma_shift(int32_t acc, int32_t x, uint8_t k)
{
    return acc + hd_shr32_round(x - acc, k);
}
