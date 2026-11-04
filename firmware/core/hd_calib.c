/*
 * HALDANE - bare-metal CO + fire safety node
 * Copyright (c) 2026 Faysal Ali Shah - MIT License
 *
 * hd_calib.c - ADC counts to physical units (see hd_calib.h).
 */
#include "hd_calib.h"
#include "hd_fixed.h"

void hd_mq7_curve_default(hd_mq_curve_t *c)
{
    c->log2_a_q16 = INT32_C(434502);     /* log2(99.042) * 2^16 */
    c->log2_clean_q16 = INT32_C(313351); /* log2(27.5)   * 2^16 */
    c->log2_rl_r0_q16 = 0;               /* RL == R0 until calibrated */
    c->b_q12 = (int16_t)-6218;           /* -1.518 * 2^12 */
}

void hd_mq135_curve_default(hd_mq_curve_t *c)
{
    c->log2_a_q16 = INT32_C(444826);     /* log2(110.47) * 2^16 */
    c->log2_clean_q16 = INT32_C(121110); /* log2(3.6)    * 2^16 */
    c->log2_rl_r0_q16 = 0;
    c->b_q12 = (int16_t)-11723;          /* -2.862 * 2^12 */
}

/* log2((1024 - ADC) / ADC) in Q16, ADC clamped to [1, 1023] counts. */
static int32_t log2_divider_q16(uint16_t adc_q4)
{
    uint32_t a = adc_q4;
    uint32_t ratio_q16;
    if (a < 16u) {
        a = 16u;
    }
    if (a > HD_ADC_Q4_MAX) {
        a = HD_ADC_Q4_MAX;
    }
    ratio_q16 = hd_udiv_frac(HD_ADC_Q4_FULL - a, a, 16u);
    return hd_log2_q16(ratio_q16);
}

int32_t hd_mq_log2_ratio_q16(const hd_mq_curve_t *c, uint16_t adc_q4)
{
    return log2_divider_q16(adc_q4) + c->log2_rl_r0_q16;
}

uint16_t hd_mq_ppm_q4(const hd_mq_curve_t *c, uint16_t adc_q4)
{
    int32_t lr = hd_mq_log2_ratio_q16(c, adc_q4);
    int32_t log2_ppm = c->log2_a_q16 + hd_mul_shr(lr, (int32_t)c->b_q12, 12u);
    uint32_t ppm_q4 = hd_pow2_q16(log2_ppm + INT32_C(4) * HD_Q16_ONE);
    return (ppm_q4 > UINT32_C(65535)) ? (uint16_t)65535u : (uint16_t)ppm_q4;
}

int32_t hd_mq_gas_index_q12(const hd_mq_curve_t *c, uint16_t adc_q4)
{
    int32_t idx_q16 = c->log2_clean_q16 - hd_mq_log2_ratio_q16(c, adc_q4);
    return hd_shr32_round(idx_q16, 4u);
}

int32_t hd_mq_calibrate_clean_air(const hd_mq_curve_t *c, uint16_t adc_q4)
{
    return c->log2_clean_q16 - log2_divider_q16(adc_q4);
}

int32_t hd_lm35_centi_c(uint16_t adc_q4, uint16_t vref_mv)
{
    /* cC = Vin_mV * 10 = adc_q4 * vref_mv * 10 / 16384 */
    int32_t num = (int32_t)adc_q4 * (int32_t)vref_mv * INT32_C(10);
    return hd_shr32_round(num, 14u);
}
