/*
 * HALDANE - bare-metal CO + fire safety node
 * Copyright (c) 2026 Faysal Ali Shah - MIT License
 *
 * hd_health.c - per-channel sensor health (see hd_health.h).
 */
#include "hd_health.h"

void hd_health_init(hd_health_ch_t *h)
{
    h->stuck_run = 0u;
    h->bad_run = 0u;
    h->good_run = 0u;
    h->faults = 0u;
}

static uint8_t instant_flags(hd_health_ch_t *h, const hd_health_cfg_t *cfg,
                             uint16_t mean_q4, uint16_t raw_min, uint16_t raw_max)
{
    uint8_t bad = 0u;
    if (cfg->open_below_q4 != 0u && mean_q4 < cfg->open_below_q4) {
        bad |= HD_HF_OPEN;
    }
    if (cfg->short_above_q4 != 0u && mean_q4 > cfg->short_above_q4) {
        bad |= HD_HF_SHORT;
    }
    if (raw_max == raw_min) {
        if (h->stuck_run < 0xFFFFu) {
            ++h->stuck_run;
        }
    } else {
        h->stuck_run = 0u;
    }
    if (cfg->stuck_s != 0u && h->stuck_run >= cfg->stuck_s) {
        bad |= HD_HF_STUCK;
    }
    return bad;
}

uint8_t hd_health_update(hd_health_ch_t *h, const hd_health_cfg_t *cfg,
                         uint16_t mean_q4, uint16_t raw_min, uint16_t raw_max)
{
    uint8_t bad = instant_flags(h, cfg, mean_q4, raw_min, raw_max);
    if (bad != 0u) {
        h->good_run = 0u;
        if (h->bad_run < 255u) {
            ++h->bad_run;
        }
        if (h->bad_run >= cfg->debounce_s) {
            h->faults = bad;
        }
    } else {
        h->bad_run = 0u;
        if (h->good_run < 255u) {
            ++h->good_run;
        }
        if (h->good_run >= cfg->recover_s) {
            h->faults = 0u;
        }
    }
    return h->faults;
}
