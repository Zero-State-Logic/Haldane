/*
 * HALDANE - host unit tests
 * Copyright (c) 2026 Faysal Ali Shah - MIT License
 *
 * bench.h - helpers that turn physical quantities into the ADC readings
 * the node expects, plus a double-precision CFK reference. Host-only.
 */
#ifndef BENCH_H
#define BENCH_H

#include "hd_node.h"

/* ADC Q4 mean for an MQ sensor at a given Rs/R0 with RL/R0 = rl_over_r0. */
uint16_t bench_mq_adc_q4(double rs_over_r0, double rl_over_r0);

/* MQ-7 ADC Q4 for a CO concentration (curve A=99.042 B=-1.518, clean 27.5). */
uint16_t bench_mq7_adc_q4(double ppm);

/* MQ-135 ADC Q4 for a gas index (doublings of conductance above clean). */
uint16_t bench_mq135_adc_q4(double gas_doublings);

/* LM35 ADC Q4 for a temperature at Vref = 5.000 V. */
uint16_t bench_lm35_adc_q4(double temp_c);

/* Fill a node input for one second, with +/-1 count of noise in min/max. */
void bench_fill(hd_node_in_t *in, double co_ppm, double temp_c, double gas_doublings);

/* A default UL 2034 config marked as calibrated (RL == R0). */
void bench_config(hd_config_t *cfg, uint8_t profile);

/* Double-precision CFK reference using the same integer parameters. */
typedef struct {
    double a;   /* 1/min */
    double b;   /* 1/(ppm*min) */
    double c;   /* 1/min */
} bench_cfk_ref_t;

void bench_cfk_ref_init(bench_cfk_ref_t *r, const hd_cfk_physio_t *p);
double bench_cfk_ref_baseline(const bench_cfk_ref_t *r);
/* RK4 step of dt_min minutes at constant ppm; x is the saturation (0..1). */
double bench_cfk_ref_step(const bench_cfk_ref_t *r, double x, double ppm, double dt_min);

#endif /* BENCH_H */
