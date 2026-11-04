/*
 * HALDANE - host C API for Python (ctypes)
 * Copyright (c) 2026 Faysal Ali Shah - MIT License
 *
 * haldane_api.c - flat, ctypes-friendly wrappers around firmware/core.
 * Host-only: the firmware never compiles this file. Python allocates the
 * node/config memory (sizes come from hd_api_sizeof) and passes pointers.
 */
#include <string.h>
#include "hd_node.h"

#if defined(_WIN32)
#define HD_API __declspec(dllexport)
#else
#define HD_API __attribute__((visibility("default")))
#endif

#define HD_API_VERSION 1

/* Node input matrix columns (uint16): mean[4], min[4], max[4], button. */
#define HD_API_IN_COLS 13

enum {
    OUT_UPTIME = 0, OUT_CO_PPM_Q4, OUT_CO_NEW, OUT_CO_VALID, OUT_COHB_X100,
    OUT_COHB_EQ_X100, OUT_TEMP_CC, OUT_GAS_Q12, OUT_STATE, OUT_SOUNDER,
    OUT_RELAY, OUT_FIRE_CAUSE, OUT_FAULTS, OUT_HEATER_DUTY, OUT_MQ7_PHASE,
    OUT_COLS
};

static const char *const k_out_names[OUT_COLS] = {
    "uptime_s", "co_ppm_q4", "co_new", "co_valid", "cohb_x100",
    "cohb_eq_x100", "temp_cc", "gas_q12", "state", "sounder",
    "relay", "fire_cause", "faults", "heater_duty", "mq7_phase"
};

/* Configuration keys for hd_api_config_set. */
enum {
    KEY_CO_ALARM_X100 = 0, KEY_CO_WARN_X100, KEY_CO_OVERRIDE_PPM_Q4,
    KEY_OCC_PRESET, KEY_CALIBRATED, KEY_MQ7_LOG2_RL_R0_Q16,
    KEY_MQ135_LOG2_RL_R0_Q16, KEY_CO_HYST_X100
};

static void physio_from_array(const int32_t v[8], hd_cfk_physio_t *p)
{
    p->vb_ml = (uint16_t)v[0];
    p->va_ml_min = (uint16_t)v[1];
    p->dl_x10 = (uint16_t)v[2];
    p->hb_g_dl_x10 = (uint16_t)v[3];
    p->haldane_m = (uint16_t)v[4];
    p->po2_mmhg = (uint16_t)v[5];
    p->vco_ul_min = (uint16_t)v[6];
    p->pb_mmhg = (uint16_t)v[7];
}

HD_API int32_t hd_api_version(void)
{
    return HD_API_VERSION;
}

HD_API int32_t hd_api_cfk_preset(int32_t id, int32_t out[8])
{
    hd_cfk_physio_t p;
    if (id < 0 || hd_cfk_physio_preset((uint8_t)id, &p) != HD_OK) {
        return HD_ERR_RANGE;
    }
    out[0] = p.vb_ml;
    out[1] = p.va_ml_min;
    out[2] = p.dl_x10;
    out[3] = p.hb_g_dl_x10;
    out[4] = p.haldane_m;
    out[5] = p.po2_mmhg;
    out[6] = p.vco_ul_min;
    out[7] = p.pb_mmhg;
    return HD_OK;
}

HD_API int32_t hd_api_cfk_coeffs(const int32_t physio[8], int32_t out[3])
{
    hd_cfk_physio_t p;
    hd_cfk_coeffs_t k;
    int8_t rc;
    physio_from_array(physio, &p);
    rc = hd_cfk_derive(&p, &k);
    if (rc != HD_OK) {
        return rc;
    }
    out[0] = k.k_ppm_q46;
    out[1] = k.k_endo_q42;
    out[2] = k.k_elim_q36;
    return HD_OK;
}

/* Integrate n seconds; init_pct_x100 < 0 starts at the zero-ppm baseline. */
HD_API int32_t hd_api_cfk_run(const int32_t physio[8], const uint16_t *ppm_q4, int32_t n,
                              int32_t init_pct_x100, uint16_t *pct_x100_out)
{
    hd_cfk_physio_t p;
    hd_cfk_coeffs_t k;
    hd_cfk_t s;
    int32_t i;
    int8_t rc;
    if (ppm_q4 == 0 || pct_x100_out == 0 || n < 0) {
        return HD_ERR_NULL;
    }
    physio_from_array(physio, &p);
    rc = hd_cfk_derive(&p, &k);
    if (rc != HD_OK) {
        return rc;
    }
    if (init_pct_x100 < 0) {
        hd_cfk_reset_baseline(&s, &k);
    } else {
        hd_cfk_set_pct_x100(&s, (uint16_t)init_pct_x100);
    }
    for (i = 0; i < n; ++i) {
        hd_cfk_step(&s, &k, ppm_q4[i]);
        pct_x100_out[i] = hd_cfk_pct_x100(&s);
    }
    return HD_OK;
}

HD_API int32_t hd_api_sizeof(int32_t which)
{
    switch (which) {
    case 0: return (int32_t)sizeof(hd_node_t);
    case 1: return (int32_t)sizeof(hd_config_t);
    case 2: return (int32_t)sizeof(hd_cfk_t);
    case 3: return (int32_t)sizeof(hd_cfk_coeffs_t);
    case 4: return (int32_t)sizeof(hd_mq7_t);
    case 5: return (int32_t)sizeof(hd_fire_t);
    case 6: return (int32_t)sizeof(hd_alarm_t);
    case 7: return (int32_t)sizeof(hd_health_ch_t) * HD_CH_COUNT;
    case 8: return (int32_t)sizeof(hd_annun_t);
    default: return -1;
    }
}

HD_API int32_t hd_api_config_default(void *cfg, int32_t profile)
{
    if (cfg == 0 || profile < 0) {
        return HD_ERR_NULL;
    }
    return hd_config_default((hd_config_t *)cfg, (uint8_t)profile);
}

HD_API int32_t hd_api_config_set(void *cfg_mem, int32_t key, int32_t value)
{
    hd_config_t *cfg = (hd_config_t *)cfg_mem;
    if (cfg == 0) {
        return HD_ERR_NULL;
    }
    switch (key) {
    case KEY_CO_ALARM_X100: cfg->alarm.co_alarm_x100 = (uint16_t)value; break;
    case KEY_CO_WARN_X100: cfg->alarm.co_warn_x100 = (uint16_t)value; break;
    case KEY_CO_OVERRIDE_PPM_Q4: cfg->alarm.co_override_ppm_q4 = (uint16_t)value; break;
    case KEY_OCC_PRESET:
        return hd_cfk_physio_preset((uint8_t)value, &cfg->occupant);
    case KEY_CALIBRATED: cfg->calibrated = (uint8_t)value; break;
    case KEY_MQ7_LOG2_RL_R0_Q16: cfg->mq7_curve.log2_rl_r0_q16 = value; break;
    case KEY_MQ135_LOG2_RL_R0_Q16: cfg->mq135_curve.log2_rl_r0_q16 = value; break;
    case KEY_CO_HYST_X100: cfg->alarm.co_hyst_x100 = (uint16_t)value; break;
    default: return HD_ERR_RANGE;
    }
    return HD_OK;
}

HD_API int32_t hd_api_node_init(void *node, const void *cfg)
{
    return hd_node_init((hd_node_t *)node, (const hd_config_t *)cfg);
}

HD_API int32_t hd_api_out_cols(void)
{
    return OUT_COLS;
}

HD_API const char *hd_api_out_col_name(int32_t i)
{
    return (i >= 0 && i < OUT_COLS) ? k_out_names[i] : "";
}

static void write_outputs(const hd_node_t *n, const hd_config_t *cfg, int32_t *o)
{
    o[OUT_UPTIME] = (int32_t)n->uptime_s;
    o[OUT_CO_PPM_Q4] = n->co_ppm_q4;
    o[OUT_CO_NEW] = n->co_new;
    o[OUT_CO_VALID] = n->co_valid;
    o[OUT_COHB_X100] = n->cohb_x100;
    o[OUT_COHB_EQ_X100] = n->cohb_eq_x100;
    o[OUT_TEMP_CC] = n->temp_cc;
    o[OUT_GAS_Q12] = n->gas_q12;
    o[OUT_STATE] = n->alarm.state;
    o[OUT_SOUNDER] = n->alarm.sounder;
    o[OUT_RELAY] = n->alarm.relay;
    o[OUT_FIRE_CAUSE] = n->fire_cause;
    o[OUT_FAULTS] = n->faults;
    o[OUT_HEATER_DUTY] = hd_node_heater_duty(n, cfg);
    o[OUT_MQ7_PHASE] = n->mq7.phase;
}

/*
 * Run n seconds. in: n x 13 uint16 (mean_q4[4], raw_min[4], raw_max[4],
 * button). flame: n x 100 raw 10-sample sums, or NULL to skip flame frames.
 * out: n x OUT_COLS int32. The heater phase reported for row i is the phase
 * in force during second i+1 (what the firmware applies next).
 */
HD_API int32_t hd_api_node_run(void *node, const void *cfg_mem, int32_t n,
                               const uint16_t *in, const uint16_t *flame, int32_t *out)
{
    hd_node_t *nd = (hd_node_t *)node;
    const hd_config_t *cfg = (const hd_config_t *)cfg_mem;
    int32_t i;
    if (nd == 0 || cfg == 0 || in == 0 || out == 0 || n < 0) {
        return HD_ERR_NULL;
    }
    for (i = 0; i < n; ++i) {
        const uint16_t *row = in + (size_t)i * HD_API_IN_COLS;
        hd_node_in_t ni;
        uint8_t ch;
        if (flame != 0) {
            int j;
            for (j = 0; j < 100; ++j) {
                hd_node_flame_frame(nd, cfg, flame[(size_t)i * 100u + (size_t)j]);
            }
        }
        for (ch = 0u; ch < (uint8_t)HD_CH_COUNT; ++ch) {
            ni.mean_q4[ch] = row[ch];
            ni.raw_min[ch] = row[4 + ch];
            ni.raw_max[ch] = row[8 + ch];
        }
        ni.button = (uint8_t)row[12];
        ni.flame_do_ms = 0u;
        hd_node_second(nd, cfg, &ni);
        write_outputs(nd, cfg, out + (size_t)i * OUT_COLS);
    }
    return HD_OK;
}
