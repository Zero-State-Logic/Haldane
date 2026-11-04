/*
 * HALDANE - bare-metal CO + fire safety node
 * Copyright (c) 2026 Faysal Ali Shah - MIT License
 *
 * hd_mq7.c - MQ-7 heater cycling and end-of-phase sampling (see hd_mq7.h).
 */
#include "hd_mq7.h"

void hd_mq7_cfg_default(hd_mq7_cfg_t *cfg)
{
    cfg->high_s = 60u;
    cfg->low_s = 90u;
    cfg->window_s = 5u;
    cfg->warmup_cycles = 2u;
    cfg->duty_high = 255u;
    cfg->duty_low = 20u;    /* (1.4 V / 5 V)^2 * 256 = 20.07 */
}

void hd_mq7_init(hd_mq7_t *m)
{
    m->window_sum_q4 = 0u;
    m->high_sum_q4 = 0u;
    m->sample_q4 = 0u;
    m->high_mean_q4 = 0u;
    m->phase = HD_MQ7_PHASE_HIGH;   /* start by cleaning the sensor */
    m->phase_s = 0u;
    m->window_n = 0u;
    m->cycles = 0u;
}

static uint8_t end_high_phase(hd_mq7_t *m, const hd_mq7_cfg_t *cfg)
{
    m->high_mean_q4 = (uint16_t)(m->high_sum_q4 / (cfg->high_s ? cfg->high_s : 1u));
    m->high_sum_q4 = 0u;
    m->phase = HD_MQ7_PHASE_LOW;
    m->phase_s = 0u;
    return HD_MQ7_EV_HIGH_DONE;
}

static uint8_t end_low_phase(hd_mq7_t *m)
{
    uint8_t ev = 0u;
    if (m->window_n > 0u) {
        m->sample_q4 = (uint16_t)(m->window_sum_q4 / m->window_n);
        ev = HD_MQ7_EV_SAMPLE;
    }
    m->window_sum_q4 = 0u;
    m->window_n = 0u;
    m->phase = HD_MQ7_PHASE_HIGH;
    m->phase_s = 0u;
    if (m->cycles < 255u) {
        ++m->cycles;
    }
    return ev;
}

uint8_t hd_mq7_second(hd_mq7_t *m, const hd_mq7_cfg_t *cfg, uint16_t adc_mean_q4)
{
    if (m->phase == HD_MQ7_PHASE_HIGH) {
        m->high_sum_q4 += adc_mean_q4;
    } else if ((uint16_t)m->phase_s + cfg->window_s >= cfg->low_s) {
        m->window_sum_q4 += adc_mean_q4;
        ++m->window_n;
    }
    ++m->phase_s;

    if (m->phase == HD_MQ7_PHASE_HIGH && m->phase_s >= cfg->high_s) {
        return end_high_phase(m, cfg);
    }
    if (m->phase == HD_MQ7_PHASE_LOW && m->phase_s >= cfg->low_s) {
        return end_low_phase(m);
    }
    return 0u;
}

uint8_t hd_mq7_heater_duty(const hd_mq7_t *m, const hd_mq7_cfg_t *cfg)
{
    return (m->phase == HD_MQ7_PHASE_HIGH) ? cfg->duty_high : cfg->duty_low;
}

uint8_t hd_mq7_is_warm(const hd_mq7_t *m, const hd_mq7_cfg_t *cfg)
{
    return (m->cycles > cfg->warmup_cycles) ? 1u : 0u;
}
