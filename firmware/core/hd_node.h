/*
 * HALDANE - bare-metal CO + fire safety node
 * Copyright (c) 2026 Faysal Ali Shah - MIT License
 *
 * hd_node.h - the whole safety node as one portable object.
 *
 * The ATmega32 firmware (firmware/avr/main.c) and the host simulator
 * (host/capi + host/haldane) drive exactly this code:
 *
 *   every 10 ms   hd_node_flame_frame()   sum of 10 flame ADC samples
 *   INT0 edge     hd_node_flame_edge()    flame comparator fired
 *   every 1 s     hd_node_second()        per-channel mean/min/max
 *
 * hd_node_second() runs health checks, the MQ-7 heater schedule, ADC to
 * units conversion, the CFK integrator, the fire detector and the alarm
 * state machine, in that order.
 */
#ifndef HD_NODE_H
#define HD_NODE_H

#include "hd_config.h"

typedef struct {
    uint16_t mean_q4[HD_CH_COUNT];  /* mean of the 1 kHz samples x16    */
    uint16_t raw_min[HD_CH_COUNT];  /* raw 10-bit min over the second   */
    uint16_t raw_max[HD_CH_COUNT];  /* raw 10-bit max over the second   */
    uint16_t flame_do_ms;           /* ms the flame comparator was on   */
    uint8_t  button;                /* HD_BTN_* event in this second    */
} hd_node_in_t;

typedef struct {
    hd_cfk_coeffs_t cfk_k;
    hd_cfk_t        cfk;
    hd_mq7_t        mq7;
    hd_fire_t       fire;
    hd_alarm_t      alarm;
    hd_health_ch_t  health[HD_CH_COUNT];
    uint32_t uptime_s;
    int32_t  temp_cc;        /* temperature, centi-degC                   */
    int32_t  gas_q12;        /* MQ-135 gas index, Q12                     */
    uint16_t co_ppm_q4;      /* held CO reading, 1/16 ppm                 */
    uint16_t cohb_x100;      /* estimated COHb, x100 %                    */
    uint16_t cohb_eq_x100;   /* COHb it would settle at, x100 %           */
    uint16_t mq135_q4;       /* last MQ-135 mean (for calibration)        */
    uint16_t faults;         /* HD_FAULT_* word                           */
    uint16_t sys_faults;     /* reset-cause bits, latched until ack       */
    uint16_t flame_edges;    /* INT0 edges in the current second          */
    uint16_t flame_edges_last; /* INT0 edges in the previous second       */
    uint16_t flame_do_ms;    /* comparator on-time in the last second     */
    uint8_t  co_new;         /* a fresh CO sample arrived this second     */
    uint8_t  co_valid;       /* CO channel warm and healthy               */
    uint8_t  heater_fault;
    uint8_t  fire_cause;     /* detector output this second               */
} hd_node_t;

/* Initialise; returns HD_OK or an error if the occupant parameters are
 * out of range. */
int8_t hd_node_init(hd_node_t *n, const hd_config_t *cfg);

/* Latch reset-cause faults (HD_FAULT_RESET_WDT / HD_FAULT_RESET_BOD). */
void hd_node_note_reset(hd_node_t *n, uint16_t sys_fault_bits);

/* 100 Hz flame frame: raw sum of 10 ADC samples (0..10230). */
void hd_node_flame_frame(hd_node_t *n, const hd_config_t *cfg, uint16_t raw_sum10);

/* INT0 edge from the flame comparator. */
void hd_node_flame_edge(hd_node_t *n, const hd_config_t *cfg);

/* One second of data. */
void hd_node_second(hd_node_t *n, const hd_config_t *cfg, const hd_node_in_t *in);

/* Heater duty to apply now (0..255). */
uint8_t hd_node_heater_duty(const hd_node_t *n, const hd_config_t *cfg);

/* Clean-air calibration from the latest MQ-7 low-phase sample and MQ-135
 * mean. Returns HD_ERR_RANGE until the MQ-7 is warm. */
int8_t hd_node_calibrate_clean_air(const hd_node_t *n, const hd_config_t *cfg,
                                   int32_t *mq7_log2_rl_r0_q16,
                                   int32_t *mq135_log2_rl_r0_q16);

#endif /* HD_NODE_H */
