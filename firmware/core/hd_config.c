/*
 * HALDANE - bare-metal CO + fire safety node
 * Copyright (c) 2026 Faysal Ali Shah - MIT License
 *
 * hd_config.c - default configuration and profiles (see hd_config.h).
 */
#include "hd_config.h"

static void health_set(hd_health_cfg_t *h, uint16_t open_q4, uint16_t short_q4,
                       uint16_t stuck_s)
{
    h->open_below_q4 = open_q4;
    h->short_above_q4 = short_q4;
    h->stuck_s = stuck_s;
    h->debounce_s = 3u;
    h->recover_s = 10u;
}

int8_t hd_config_default(hd_config_t *cfg, uint8_t profile)
{
    int8_t rc;
    if (cfg == 0) {
        return HD_ERR_NULL;
    }
    if (profile >= (uint8_t)HD_PROFILE_COUNT) {
        return HD_ERR_RANGE;
    }
    rc = hd_cfk_physio_preset(HD_OCC_HEAVY, &cfg->occupant);
    if (rc != HD_OK) {
        return rc;
    }
    hd_alarm_cfg_default(&cfg->alarm);
    hd_fire_cfg_default(&cfg->fire);
    hd_mq7_cfg_default(&cfg->mq7);
    hd_mq7_curve_default(&cfg->mq7_curve);
    hd_mq135_curve_default(&cfg->mq135_curve);

    /* MQ: open < 4 counts, short > 1015 counts. LM35 (with its output
     * pull-down): open < 2 counts (~1 degC; the basic LM35 circuit cannot
     * read below +2 degC anyway), short > 1000 counts. Stuck: 10 min of
     * bit-identical samples. The flame module legitimately saturates in
     * sunlight, so it gets no rail or stuck checks. */
    health_set(&cfg->health[HD_CH_MQ7], 64u, 16240u, 600u);
    health_set(&cfg->health[HD_CH_MQ135], 64u, 16240u, 600u);
    health_set(&cfg->health[HD_CH_LM35], 32u, 16000u, 600u);
    health_set(&cfg->health[HD_CH_FLAME], 0u, 0u, 0u);

    cfg->vref_mv = 5000u;
    cfg->heater_min_q4 = 128u;   /* 8 counts */
    cfg->flame_inverted = 1u;
    cfg->calibrated = 0u;
    cfg->profile = profile;

    if (profile == HD_PROFILE_SENSITIVE) {
        cfg->alarm.co_alarm_x100 = 330u;
        cfg->alarm.co_warn_x100 = 200u;
        /* 250 ppm: the MQ-7 reads a few % low at the end of its 90 s low
         * phase, so a 300 ppm target needs headroom */
        cfg->alarm.co_override_ppm_q4 = 250u * 16u;
    }
    return HD_OK;
}
