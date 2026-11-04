/*
 * HALDANE - bare-metal CO + fire safety node
 * Copyright (c) 2026 Faysal Ali Shah - MIT License
 *
 * hd_fire.h - fused multi-sensor fire detection on 8 bits.
 *
 * Four evidence channels, each turned into a one-sided CUSUM (Page,
 * Biometrika 1954): S = max(0, S + z - k). k is a drift allowance (what
 * a normal room can do), S is "excess evidence beyond that drift".
 *
 *   temperature  rise of an EWMA-smoothed LM35 reading      (centi-degC)
 *   MQ-135       rise of the gas index log2(clean/Rs/R0)    (Q12 doublings)
 *   CO           rise between MQ-7 samples (every 150 s)    (1/16 ppm)
 *   flame        relative flicker energy in ~2-15 Hz from 100 Hz samples
 *                of the IR module. Flames flicker; sunlight and radiant
 *                heaters do not.
 *
 * Rules (first match wins, reported as the cause):
 *   HEAT_FIXED   T >= 57 degC for fixed_hold_s            (backup)
 *   HEAT_ROR     rate-of-rise >= 8.3 degC/min for ror_hold_s (backup)
 *   FLAME_FAST   INT0-armed: strong flicker + strong IR for ~1.5 s
 *   FLAMING      flicker evidence AND half-evidence on heat, gas or CO
 *   SMOULDERING  CO evidence AND gas evidence
 *   SMOKE_HEAT   gas evidence AND heat evidence (flame out of view)
 */
#ifndef HD_FIRE_H
#define HD_FIRE_H

#include "hd_port.h"

enum {
    HD_FIRE_NONE = 0,
    HD_FIRE_HEAT_FIXED = 1,
    HD_FIRE_HEAT_ROR = 2,
    HD_FIRE_FLAME_FAST = 3,
    HD_FIRE_FLAMING = 4,
    HD_FIRE_SMOULDERING = 5,
    HD_FIRE_SMOKE_HEAT = 6
};

#define HD_FIRE_V_TEMP   0x01u
#define HD_FIRE_V_GAS    0x02u
#define HD_FIRE_V_CO     0x04u
#define HD_FIRE_V_FLAME  0x08u

typedef struct {
    /* flame DSP at 100 Hz (IR units = sum of 10 ADC samples, 0..10230) */
    uint8_t  lp_fast_a_q8;       /* 156/256: 15 Hz corner                 */
    uint8_t  lp_slow_a_q8;       /* 30/256: 2 Hz corner                   */
    uint8_t  energy_k;           /* EWMA shift for |band| (6 = 0.64 s)    */
    uint8_t  level_k;            /* EWMA shift for IR level (7 = 1.28 s)  */
    uint16_t level_floor;        /* relative-flicker denominator floor    */
    uint16_t energy_min;         /* minimum absolute flicker energy       */
    uint16_t flame_level_min;    /* IR level needed for flame evidence    */
    uint16_t flk_k_q8;           /* CUSUM allowance, relative flicker Q8  */
    uint16_t flk_h;              /* CUSUM threshold, Q8-seconds           */
    uint16_t fast_level;         /* FLAME_FAST: IR level                  */
    uint16_t fast_rel_q8;        /* FLAME_FAST: relative flicker          */
    uint16_t fast_confirm_ticks; /* FLAME_FAST: 100 Hz frames required    */
    uint16_t fast_window_ticks;  /* FLAME_FAST: window after an INT0 edge */
    /* temperature */
    uint8_t  temp_smooth_k;      /* EWMA shift of the smoothed reading    */
    uint8_t  ror_k;              /* EWMA shift of the rate-of-rise        */
    uint8_t  fixed_hold_s;
    uint8_t  ror_hold_s;
    int16_t  temp_k_cc_min;      /* allowance, centi-degC per minute      */
    int16_t  temp_h_cc;          /* threshold, centi-degC of excess rise  */
    int16_t  fixed_cc;           /* fixed-temperature backup, centi-degC  */
    int16_t  ror_cc_min;         /* rate-of-rise backup, centi-degC/min   */
    /* MQ-135 gas index */
    uint8_t  gas_smooth_k;
    int16_t  gas_k_q12_min;      /* allowance, Q12 per minute             */
    int16_t  gas_h_q12;          /* threshold, Q12                        */
    /* CO */
    uint16_t co_k_q4_min;        /* allowance, 1/16 ppm per minute        */
    uint16_t co_h_q4;            /* threshold, 1/16 ppm                   */
} hd_fire_cfg_t;

typedef struct {
    int32_t temp_cc;      /* this second's temperature, centi-degC      */
    int32_t gas_q12;      /* MQ-135 gas index, Q12                     */
    uint16_t co_ppm_q4;   /* latest CO reading                         */
    uint8_t co_new;       /* 1 if co_ppm_q4 is a fresh MQ-7 sample      */
    uint8_t valid;        /* HD_FIRE_V_* channels that are healthy      */
} hd_fire_in_t;

typedef struct {
    /* 100 Hz flame DSP */
    int32_t lp_fast_q4;
    int32_t lp_slow_q4;
    int32_t energy_q8;
    int32_t level_q8;
    /* 1 Hz features */
    int32_t temp_s_q4;    /* smoothed temperature, centi-degC Q4        */
    int32_t ror_q4;       /* rate-of-rise, centi-degC/min Q4            */
    int32_t gas_s_q12;    /* smoothed gas index                         */
    /* CUSUMs (scaled by 60 so per-minute allowances stay integral) */
    int32_t s_temp;
    int32_t s_gas;
    int32_t s_co;
    int32_t s_flk;
    uint16_t co_prev_q4;
    uint16_t co_dt_s;     /* seconds since the previous CO sample       */
    uint16_t fast_window; /* FLAME_FAST frames left in the armed window */
    uint16_t fast_count;  /* consecutive strong frames                  */
    uint8_t fixed_run;
    uint8_t ror_run;
    uint8_t fast_fire;    /* FLAME_FAST latched until the next second   */
    uint8_t primed;       /* bit0 temp, bit1 gas, bit2 co               */
    uint8_t cause;        /* last evaluated cause                       */
    uint8_t rel_q8;       /* last relative flicker (saturated), telemetry */
} hd_fire_t;

void hd_fire_cfg_default(hd_fire_cfg_t *cfg);
void hd_fire_init(hd_fire_t *f);

/* One 100 Hz flame frame (IR units, larger = more IR). */
void hd_fire_flame_frame(hd_fire_t *f, const hd_fire_cfg_t *cfg, uint16_t ir);

/* INT0 edge from the flame comparator: arm the fast confirmation window. */
void hd_fire_flame_edge(hd_fire_t *f, const hd_fire_cfg_t *cfg);

/* One second of evidence; returns the HD_FIRE_* cause (0 = no fire). */
uint8_t hd_fire_second(hd_fire_t *f, const hd_fire_cfg_t *cfg, const hd_fire_in_t *in);

/* 1 if a hush (silence) may be granted for this cause. */
uint8_t hd_fire_hush_allowed(uint8_t cause);

/* Telemetry helpers (IR units). */
uint16_t hd_fire_flame_level(const hd_fire_t *f);
uint16_t hd_fire_flame_energy(const hd_fire_t *f);

#endif /* HD_FIRE_H */
