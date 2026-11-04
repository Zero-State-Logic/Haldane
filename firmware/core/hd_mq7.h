/*
 * HALDANE - bare-metal CO + fire safety node
 * Copyright (c) 2026 Faysal Ali Shah - MIT License
 *
 * hd_mq7.h - MQ-7 heater cycling and end-of-phase sampling.
 *
 * The MQ-7 datasheet operates the heater in two phases: 60 s at 5.0 V
 * (burns off adsorbed gas) and 90 s at 1.4 V (the sensing phase). CO is
 * read at the END of the low phase. The firmware switches the heater
 * through a logic-level MOSFET: full on for the high phase and PWM in the
 * low phase. Heater power goes as V^2, so 1.4 V from a 5 V rail needs a
 * duty of (1.4/5)^2 = 7.8 % (20/256), not 28 %.
 *
 * Consequence for everything downstream: the CO reading updates once per
 * 150 s cycle. The CFK integrator holds the last valid reading.
 */
#ifndef HD_MQ7_H
#define HD_MQ7_H

#include "hd_port.h"

#define HD_MQ7_PHASE_HIGH  0u
#define HD_MQ7_PHASE_LOW   1u

#define HD_MQ7_EV_SAMPLE     0x01u   /* a new end-of-low-phase reading      */
#define HD_MQ7_EV_HIGH_DONE  0x02u   /* a high phase ended, high_mean valid */

typedef struct {
    uint8_t high_s;         /* high (5 V) phase length, s               */
    uint8_t low_s;          /* low (1.4 V) phase length, s              */
    uint8_t window_s;       /* readings averaged at the end of low phase */
    uint8_t warmup_cycles;  /* cycles discarded after power-up          */
    uint8_t duty_high;      /* heater duty in the high phase (255 = DC) */
    uint8_t duty_low;       /* heater duty in the low phase (x/256)     */
} hd_mq7_cfg_t;

typedef struct {
    uint32_t window_sum_q4;
    uint32_t high_sum_q4;
    uint16_t sample_q4;     /* last published end-of-low-phase reading */
    uint16_t high_mean_q4;  /* mean reading over the last high phase   */
    uint8_t  phase;
    uint8_t  phase_s;       /* seconds completed in the current phase  */
    uint8_t  window_n;
    uint8_t  cycles;        /* completed cycles, saturating at 255     */
} hd_mq7_t;

void hd_mq7_cfg_default(hd_mq7_cfg_t *cfg);
void hd_mq7_init(hd_mq7_t *m);

/* Feed the mean reading of the second that just ended (taken in the
 * current phase). Returns HD_MQ7_EV_* flags. */
uint8_t hd_mq7_second(hd_mq7_t *m, const hd_mq7_cfg_t *cfg, uint16_t adc_mean_q4);

/* Heater duty to apply now (0..255). */
uint8_t hd_mq7_heater_duty(const hd_mq7_t *m, const hd_mq7_cfg_t *cfg);

/* 1 once the warm-up cycles are done. */
uint8_t hd_mq7_is_warm(const hd_mq7_t *m, const hd_mq7_cfg_t *cfg);

#endif /* HD_MQ7_H */
