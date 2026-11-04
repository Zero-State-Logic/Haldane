/*
 * HALDANE - bare-metal CO + fire safety node
 * Copyright (c) 2026 Faysal Ali Shah - MIT License
 *
 * hd_node.c - the whole safety node as one portable object (see hd_node.h).
 */
#include "hd_node.h"

int8_t hd_node_init(hd_node_t *n, const hd_config_t *cfg)
{
    uint8_t ch;
    int8_t rc;
    if (n == 0 || cfg == 0) {
        return HD_ERR_NULL;
    }
    rc = hd_cfk_derive(&cfg->occupant, &n->cfk_k);
    if (rc != HD_OK) {
        return rc;
    }
    hd_cfk_reset_baseline(&n->cfk, &n->cfk_k);
    hd_mq7_init(&n->mq7);
    hd_fire_init(&n->fire);
    hd_alarm_init(&n->alarm);
    for (ch = 0u; ch < (uint8_t)HD_CH_COUNT; ++ch) {
        hd_health_init(&n->health[ch]);
    }
    n->uptime_s = 0u;
    n->temp_cc = 0;
    n->gas_q12 = 0;
    n->co_ppm_q4 = 0u;
    n->cohb_x100 = hd_cfk_pct_x100(&n->cfk);
    n->cohb_eq_x100 = n->cohb_x100;
    n->mq135_q4 = 0u;
    n->faults = 0u;
    n->sys_faults = 0u;
    n->flame_edges = 0u;
    n->flame_edges_last = 0u;
    n->flame_do_ms = 0u;
    n->co_new = 0u;
    n->co_valid = 0u;
    n->heater_fault = 0u;
    n->fire_cause = HD_FIRE_NONE;
    return HD_OK;
}

void hd_node_note_reset(hd_node_t *n, uint16_t sys_fault_bits)
{
    n->sys_faults |= (uint16_t)(sys_fault_bits & (HD_FAULT_RESET_WDT | HD_FAULT_RESET_BOD));
}

void hd_node_flame_frame(hd_node_t *n, const hd_config_t *cfg, uint16_t raw_sum10)
{
    uint16_t ir = raw_sum10;
    if (ir > HD_FLAME_FRAME_MAX) {
        ir = HD_FLAME_FRAME_MAX;
    }
    if (cfg->flame_inverted) {
        ir = (uint16_t)(HD_FLAME_FRAME_MAX - ir);
    }
    hd_fire_flame_frame(&n->fire, &cfg->fire, ir);
    if (n->fire.fast_fire && n->alarm.fire != HD_FS_ALARM) {
        hd_alarm_force_fire(&n->alarm, HD_FIRE_FLAME_FAST);
    }
}

void hd_node_flame_edge(hd_node_t *n, const hd_config_t *cfg)
{
    if (n->flame_edges < 0xFFFFu) {
        ++n->flame_edges;
    }
    hd_fire_flame_edge(&n->fire, &cfg->fire);
}

static uint16_t update_health(hd_node_t *n, const hd_config_t *cfg, const hd_node_in_t *in)
{
    static const HD_ROM uint8_t shift[HD_CH_COUNT] = {
        HD_FAULT_SHIFT_MQ7, HD_FAULT_SHIFT_MQ135, HD_FAULT_SHIFT_LM35, HD_FAULT_SHIFT_FLAME
    };
    uint16_t faults = 0u;
    uint8_t ch;
    for (ch = 0u; ch < (uint8_t)HD_CH_COUNT; ++ch) {
        uint8_t f = hd_health_update(&n->health[ch], &cfg->health[ch], in->mean_q4[ch],
                                     in->raw_min[ch], in->raw_max[ch]);
        faults |= (uint16_t)((uint16_t)f << shift[ch]);
    }
    return faults;
}

static uint8_t channel_ok(const hd_node_t *n, uint8_t ch)
{
    return (uint8_t)(n->health[ch].faults == 0u);
}

static void co_path(hd_node_t *n, const hd_config_t *cfg, uint16_t mq7_mean_q4)
{
    uint8_t ev = hd_mq7_second(&n->mq7, &cfg->mq7, mq7_mean_q4);
    uint8_t ok;
    if ((ev & HD_MQ7_EV_HIGH_DONE) != 0u) {
        n->heater_fault = (uint8_t)(n->mq7.high_mean_q4 < cfg->heater_min_q4);
    }
    ok = (uint8_t)(channel_ok(n, HD_CH_MQ7) && !n->heater_fault &&
                   hd_mq7_is_warm(&n->mq7, &cfg->mq7));
    n->co_new = 0u;
    if (!ok) {
        n->co_ppm_q4 = 0u;          /* unknown CO: elimination only */
    } else if ((ev & HD_MQ7_EV_SAMPLE) != 0u) {
        n->co_ppm_q4 = hd_mq_ppm_q4(&cfg->mq7_curve, n->mq7.sample_q4);
        n->co_new = 1u;
    }
    n->co_valid = ok;
    hd_cfk_step(&n->cfk, &n->cfk_k, n->co_ppm_q4);
    n->cohb_x100 = hd_cfk_pct_x100(&n->cfk);
    n->cohb_eq_x100 = hd_cfk_equilibrium_pct_x100(&n->cfk_k, n->co_ppm_q4);
}

static uint8_t fire_path(hd_node_t *n, const hd_config_t *cfg, const hd_node_in_t *in)
{
    hd_fire_in_t fin;
    fin.valid = 0u;
    n->mq135_q4 = in->mean_q4[HD_CH_MQ135];
    if (channel_ok(n, HD_CH_LM35)) {
        n->temp_cc = hd_lm35_centi_c(in->mean_q4[HD_CH_LM35], cfg->vref_mv);
        fin.valid |= HD_FIRE_V_TEMP;
    }
    if (channel_ok(n, HD_CH_MQ135)) {
        n->gas_q12 = hd_mq_gas_index_q12(&cfg->mq135_curve, in->mean_q4[HD_CH_MQ135]);
        fin.valid |= HD_FIRE_V_GAS;
    }
    if (n->co_valid) {
        fin.valid |= HD_FIRE_V_CO;
    }
    if (channel_ok(n, HD_CH_FLAME)) {
        fin.valid |= HD_FIRE_V_FLAME;
    }
    fin.temp_cc = n->temp_cc;
    fin.gas_q12 = n->gas_q12;
    fin.co_ppm_q4 = n->co_ppm_q4;
    fin.co_new = n->co_new;
    return hd_fire_second(&n->fire, &cfg->fire, &fin);
}

void hd_node_second(hd_node_t *n, const hd_config_t *cfg, const hd_node_in_t *in)
{
    hd_alarm_in_t ain;
    uint16_t faults;

    ++n->uptime_s;
    faults = update_health(n, cfg, in);
    co_path(n, cfg, in->mean_q4[HD_CH_MQ7]);
    n->fire_cause = fire_path(n, cfg, in);

    if (n->heater_fault) {
        faults |= HD_FAULT_MQ7_HEATER;
    }
    if (!cfg->calibrated) {
        faults |= HD_FAULT_UNCALIBRATED;
    }
    n->faults = (uint16_t)(faults | n->sys_faults);
    n->flame_do_ms = in->flame_do_ms;
    n->flame_edges_last = n->flame_edges;
    n->flame_edges = 0u;

    ain.cohb_x100 = n->cohb_x100;
    ain.co_ppm_q4 = n->co_ppm_q4;
    ain.faults = n->faults;
    ain.co_valid = n->co_valid;
    ain.warming = (uint8_t)!hd_mq7_is_warm(&n->mq7, &cfg->mq7);
    ain.fire_cause = n->fire_cause;
    ain.button = in->button;
    hd_alarm_step(&n->alarm, &cfg->alarm, &ain);
    if (n->alarm.ack) {
        n->sys_faults = 0u;
    }
}

uint8_t hd_node_heater_duty(const hd_node_t *n, const hd_config_t *cfg)
{
    return hd_mq7_heater_duty(&n->mq7, &cfg->mq7);
}

int8_t hd_node_calibrate_clean_air(const hd_node_t *n, const hd_config_t *cfg,
                                   int32_t *mq7_log2_rl_r0_q16,
                                   int32_t *mq135_log2_rl_r0_q16)
{
    if (n == 0 || cfg == 0 || mq7_log2_rl_r0_q16 == 0 || mq135_log2_rl_r0_q16 == 0) {
        return HD_ERR_NULL;
    }
    if (!hd_mq7_is_warm(&n->mq7, &cfg->mq7) || n->mq7.sample_q4 == 0u || n->mq135_q4 == 0u) {
        return HD_ERR_RANGE;
    }
    *mq7_log2_rl_r0_q16 = hd_mq_calibrate_clean_air(&cfg->mq7_curve, n->mq7.sample_q4);
    *mq135_log2_rl_r0_q16 = hd_mq_calibrate_clean_air(&cfg->mq135_curve, n->mq135_q4);
    return HD_OK;
}
