/*
 * HALDANE - bare-metal CO + fire safety node
 * Copyright (c) 2026 Faysal Ali Shah - MIT License
 *
 * hd_calib.h - ADC counts to physical units, integer only.
 *
 * ADC readings arrive as "Q4 means": the mean of many 10-bit samples
 * times 16 (0 .. 16368). The ATmega32 ADC gives ADC = Vin * 1024 / Vref.
 *
 * MQ sensors (ratiometric, module powered from AVCC):
 *   Rs = RL * (1024 - ADC) / ADC
 *   ppm = A * (Rs/R0)^B   ->   log2 ppm = log2 A + B * log2(Rs/R0)
 *   log2(Rs/R0) = log2((1024 - ADC)/ADC) + log2(RL/R0)
 * so the per-unit calibration is a single number, log2(RL/R0), found in
 * clean air where Rs/R0 equals the datasheet clean-air ratio.
 *
 * LM35: 10 mV/degC, Vin = ADC * Vref / 1024.
 */
#ifndef HD_CALIB_H
#define HD_CALIB_H

#include "hd_port.h"

#define HD_ADC_Q4_FULL   16384u   /* 1024 * 16 */
#define HD_ADC_Q4_MAX    16368u   /* 1023 * 16 */

/* Power-law gas curve + per-unit calibration. */
typedef struct {
    int32_t log2_a_q16;      /* log2(A)                                  */
    int32_t log2_clean_q16;  /* log2(datasheet Rs/R0 in clean air)       */
    int32_t log2_rl_r0_q16;  /* log2(RL/R0), from clean-air calibration  */
    int16_t b_q12;           /* exponent B (negative for these sensors)  */
} hd_mq_curve_t;

/* Datasheet-curve fits (same constants as the open-source
 * MQUnifiedsensor library): MQ-7 CO A=99.042 B=-1.518, clean ratio 27.5;
 * MQ-135 "CO2" A=110.47 B=-2.862, clean ratio 3.6. RL/R0 defaults to 1. */
void hd_mq7_curve_default(hd_mq_curve_t *c);
void hd_mq135_curve_default(hd_mq_curve_t *c);

/* log2(Rs/R0) in Q16. The ADC value is clamped one count inside the rails. */
int32_t hd_mq_log2_ratio_q16(const hd_mq_curve_t *c, uint16_t adc_q4);

/* Gas concentration in 1/16 ppm, saturating at 65535 (4095.9 ppm). */
uint16_t hd_mq_ppm_q4(const hd_mq_curve_t *c, uint16_t adc_q4);

/* Gas index: doublings of conductance above clean air,
 * log2(clean ratio) - log2(Rs/R0), in Q12. 0 in clean air, rises with gas. */
int32_t hd_mq_gas_index_q12(const hd_mq_curve_t *c, uint16_t adc_q4);

/* Clean-air calibration: the log2(RL/R0) that makes this reading equal the
 * datasheet clean-air ratio. Store it in c->log2_rl_r0_q16. */
int32_t hd_mq_calibrate_clean_air(const hd_mq_curve_t *c, uint16_t adc_q4);

/* LM35 temperature in centi-degC for a reference voltage in mV. */
int32_t hd_lm35_centi_c(uint16_t adc_q4, uint16_t vref_mv);

#endif /* HD_CALIB_H */
