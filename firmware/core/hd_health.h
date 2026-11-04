/*
 * HALDANE - bare-metal CO + fire safety node
 * Copyright (c) 2026 Faysal Ali Shah - MIT License
 *
 * hd_health.h - per-channel sensor health: open, short, stuck.
 *
 * Every second each ADC channel reports its mean and the raw min/max of
 * the ~1000 samples taken that second. A healthy analog front end always
 * shows at least a count or two of noise, so a channel whose samples are
 * bit-identical for minutes is treated as stuck (frozen ADC, shorted
 * node, disconnected input held by a capacitor). Means pinned to a rail
 * indicate an open or shorted sensor. Faults are debounced both ways.
 */
#ifndef HD_HEALTH_H
#define HD_HEALTH_H

#include "hd_port.h"

#define HD_HF_OPEN   0x01u
#define HD_HF_SHORT  0x02u
#define HD_HF_STUCK  0x04u
#define HD_HF_MASK   0x07u

/* Node-level fault word: 3 bits per channel plus system bits. */
#define HD_FAULT_SHIFT_MQ7     0u
#define HD_FAULT_SHIFT_MQ135   3u
#define HD_FAULT_SHIFT_LM35    6u
#define HD_FAULT_SHIFT_FLAME   9u
#define HD_FAULT_MQ7_HEATER    0x1000u
#define HD_FAULT_RESET_WDT     0x2000u
#define HD_FAULT_RESET_BOD     0x4000u
#define HD_FAULT_UNCALIBRATED  0x8000u

typedef struct {
    uint16_t open_below_q4;   /* mean below this = open circuit (0 = off)  */
    uint16_t short_above_q4;  /* mean above this = shorted (0 = off)       */
    uint16_t stuck_s;         /* zero-spread seconds before stuck (0 = off) */
    uint8_t  debounce_s;      /* bad seconds before a fault is raised      */
    uint8_t  recover_s;       /* good seconds before it clears             */
} hd_health_cfg_t;

typedef struct {
    uint16_t stuck_run;
    uint8_t  bad_run;
    uint8_t  good_run;
    uint8_t  faults;          /* HD_HF_* */
} hd_health_ch_t;

void hd_health_init(hd_health_ch_t *h);

/* Update with one second of data; returns the debounced HD_HF_* flags. */
uint8_t hd_health_update(hd_health_ch_t *h, const hd_health_cfg_t *cfg,
                         uint16_t mean_q4, uint16_t raw_min, uint16_t raw_max);

#endif /* HD_HEALTH_H */
