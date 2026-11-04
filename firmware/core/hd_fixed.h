/*
 * HALDANE - bare-metal CO + fire safety node
 * Copyright (c) 2026 Faysal Ali Shah - MIT License
 *
 * hd_fixed.h - fixed-point helpers shared by every core module.
 *
 * Conventions: a value "in Qn" is stored as round(value * 2^n) in a
 * 32-bit integer. Products of two 32-bit operands use a 32x32->64
 * multiply followed by a rounding shift (the same thing ARM's SMULL
 * does). There is no 64-bit division anywhere in this file; fractional
 * division uses a 32-bit shift-subtract (restoring) divider.
 */
#ifndef HD_FIXED_H
#define HD_FIXED_H

#include "hd_port.h"

#define HD_Q16_ONE  (INT32_C(1) << 16)
#define HD_Q30_ONE  (INT32_C(1) << 30)

/* Symmetric round-half-away-from-zero right shift (C99 leaves >> of a
 * negative value implementation-defined, so we never rely on it). */
int32_t hd_shr32_round(int32_t v, uint8_t s);
int64_t hd_shr64_round(int64_t v, uint8_t s);

/* (a * b) >> s with rounding, 64-bit intermediate, saturated to int32. */
int32_t hd_mul_shr(int32_t a, int32_t b, uint8_t s);

int32_t hd_clamp32(int32_t v, int32_t lo, int32_t hi);
int32_t hd_sat_add32(int32_t a, int32_t b);

/*
 * floor((num << frac_bits) / den) using only 32-bit operations.
 * Preconditions: den != 0, den < 2^31. Returns UINT32_MAX when the
 * quotient does not fit (or den == 0).
 */
uint32_t hd_udiv_frac(uint32_t num, uint32_t den, uint8_t frac_bits);

/* log2(x / 2^16) in Q16 for x > 0 (x is a Q16 value). x == 0 returns
 * INT32_MIN. Table + linear interpolation, |error| < 2e-4. */
int32_t hd_log2_q16(uint32_t x_q16);

/* round(2^(y / 2^16)) as an integer, saturating at UINT32_MAX and
 * returning 0 when the result would be below 0.5. */
uint32_t hd_pow2_q16(int32_t y_q16);

/* acc + alpha * (x - acc) with alpha in Q8 (0..256), rounded. */
int32_t hd_ewma_q8(int32_t acc, int32_t x, uint8_t alpha_q8);

/* acc + (x - acc) / 2^k, rounded (alpha = 2^-k). */
int32_t hd_ewma_shift(int32_t acc, int32_t x, uint8_t k);

#endif /* HD_FIXED_H */
