/*
 * HALDANE - host unit tests
 * Copyright (c) 2026 Faysal Ali Shah - MIT License
 *
 * bench.c - physical-to-ADC helpers and the double CFK reference.
 */
#include "bench.h"
#include <math.h>

static uint16_t clamp_q4(double v)
{
    if (v < 0.0) {
        v = 0.0;
    }
    if (v > 16368.0) {
        v = 16368.0;
    }
    return (uint16_t)(v + 0.5);
}

uint16_t bench_mq_adc_q4(double rs_over_r0, double rl_over_r0)
{
    /* ADC = 1024 * RL / (RL + Rs) */
    double adc = 1024.0 * rl_over_r0 / (rl_over_r0 + rs_over_r0);
    return clamp_q4(adc * 16.0);
}

uint16_t bench_mq7_adc_q4(double ppm)
{
    double ratio = 27.5;
    if (ppm > 0.5) {
        ratio = pow(ppm / 99.042, 1.0 / -1.518);
        if (ratio > 27.5) {
            ratio = 27.5;
        }
    }
    return bench_mq_adc_q4(ratio, 1.0);
}

uint16_t bench_mq135_adc_q4(double gas_doublings)
{
    return bench_mq_adc_q4(3.6 / pow(2.0, gas_doublings), 1.0);
}

uint16_t bench_lm35_adc_q4(double temp_c)
{
    return clamp_q4(temp_c * 10.0 * 1024.0 / 5000.0 * 16.0);
}

static void fill_channel(hd_node_in_t *in, uint8_t ch, uint16_t q4)
{
    uint16_t raw = (uint16_t)(q4 / 16u);
    in->mean_q4[ch] = q4;
    in->raw_min[ch] = (raw > 0u) ? (uint16_t)(raw - 1u) : 0u;
    in->raw_max[ch] = (raw < 1023u) ? (uint16_t)(raw + 1u) : 1023u;
}

void bench_fill(hd_node_in_t *in, double co_ppm, double temp_c, double gas_doublings)
{
    fill_channel(in, HD_CH_MQ7, bench_mq7_adc_q4(co_ppm));
    fill_channel(in, HD_CH_MQ135, bench_mq135_adc_q4(gas_doublings));
    fill_channel(in, HD_CH_LM35, bench_lm35_adc_q4(temp_c));
    fill_channel(in, HD_CH_FLAME, (uint16_t)(1000u * 16u));   /* dark room */
    in->flame_do_ms = 0u;
    in->button = HD_BTN_NONE;
}

void bench_config(hd_config_t *cfg, uint8_t profile)
{
    (void)hd_config_default(cfg, profile);
    cfg->calibrated = 1u;
}

void bench_cfk_ref_init(bench_cfk_ref_t *r, const hd_cfk_physio_t *p)
{
    double hbmax = 1.38 * (p->hb_g_dl_x10 / 10.0) / 100.0;
    double pl = (double)p->pb_mmhg - 47.0;
    double dl = p->dl_x10 / 10.0;
    double bb = 1.0 / dl + pl / (double)p->va_ml_min;
    double vb = p->vb_ml;
    r->a = (p->vco_ul_min / 1000.0) / (vb * hbmax);
    r->b = (1e-6 * pl) / (bb * vb * hbmax);
    r->c = (double)p->po2_mmhg / ((double)p->haldane_m * bb * vb * hbmax);
}

double bench_cfk_ref_baseline(const bench_cfk_ref_t *r)
{
    return r->a / (r->a + r->c);
}

static double ref_rate(const bench_cfk_ref_t *r, double x, double ppm)
{
    return r->a + r->b * ppm - r->c * x / (1.0 - x);
}

double bench_cfk_ref_step(const bench_cfk_ref_t *r, double x, double ppm, double dt_min)
{
    double k1 = ref_rate(r, x, ppm);
    double k2 = ref_rate(r, x + 0.5 * dt_min * k1, ppm);
    double k3 = ref_rate(r, x + 0.5 * dt_min * k2, ppm);
    double k4 = ref_rate(r, x + dt_min * k3, ppm);
    return x + dt_min / 6.0 * (k1 + 2.0 * k2 + 2.0 * k3 + k4);
}
