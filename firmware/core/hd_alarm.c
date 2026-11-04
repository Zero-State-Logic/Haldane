/*
 * HALDANE - bare-metal CO + fire safety node
 * Copyright (c) 2026 Faysal Ali Shah - MIT License
 *
 * hd_alarm.c - alarm state machine and annunciator (see hd_alarm.h).
 */
#include "hd_alarm.h"
#include "hd_fire.h"

void hd_alarm_cfg_default(hd_alarm_cfg_t *cfg)
{
    cfg->co_alarm_x100 = 550u;      /* 5.50 % COHb, see docs/CFK.md      */
    cfg->co_warn_x100 = 300u;       /* 3.00 % COHb pre-alarm: fan on     */
    cfg->co_hyst_x100 = 50u;
    cfg->co_no_hush_x100 = 1000u;   /* the 10 % line: no silencing above */
    cfg->co_override_ppm_q4 = 0u;   /* off in the UL 2034 profile        */
    cfg->co_hush_s = 300u;
    cfg->fire_hush_s = 600u;
    cfg->fire_min_on_s = 30u;
    cfg->fire_clear_s = 60u;
    cfg->test_s = 6u;
}

void hd_alarm_init(hd_alarm_t *a)
{
    a->co_hush_left = 0u;
    a->fire_hush_left = 0u;
    a->fire_on_s = 0u;
    a->fire_clear_run = 0u;
    a->co = HD_CO_IDLE;
    a->fire = HD_FS_IDLE;
    a->fire_cause = HD_FIRE_NONE;
    a->memory = 0u;
    a->test_left = 0u;
    a->state = HD_STATE_NORMAL;
    a->sounder = HD_SND_NONE;
    a->relay = 0u;
    a->ack = 0u;
}

static uint16_t inc_u16(uint16_t v)
{
    return (v < 0xFFFFu) ? (uint16_t)(v + 1u) : v;
}

/* ---------------------------------------------------------------- CO --- */

static uint8_t co_override_hit(const hd_alarm_cfg_t *cfg, const hd_alarm_in_t *in)
{
    return (uint8_t)(cfg->co_override_ppm_q4 != 0u && in->co_valid &&
                     in->co_ppm_q4 >= cfg->co_override_ppm_q4);
}

static uint8_t co_quiet_state(const hd_alarm_t *a, const hd_alarm_cfg_t *cfg, uint16_t cohb)
{
    uint16_t off = (cfg->co_warn_x100 > cfg->co_hyst_x100)
                       ? (uint16_t)(cfg->co_warn_x100 - cfg->co_hyst_x100) : 0u;
    if (cohb >= cfg->co_warn_x100) {
        return HD_CO_WARN;
    }
    if (a->co == HD_CO_WARN && cohb >= off) {
        return HD_CO_WARN;
    }
    return HD_CO_IDLE;
}

static void co_step(hd_alarm_t *a, const hd_alarm_cfg_t *cfg, const hd_alarm_in_t *in)
{
    uint16_t cohb = in->cohb_x100;
    uint16_t clear_level = (cfg->co_alarm_x100 > cfg->co_hyst_x100)
                               ? (uint16_t)(cfg->co_alarm_x100 - cfg->co_hyst_x100) : 0u;
    uint8_t raise = (uint8_t)(cohb >= cfg->co_alarm_x100 || co_override_hit(cfg, in));
    uint8_t clear = (uint8_t)(cohb < clear_level && !co_override_hit(cfg, in));

    switch (a->co) {
    case HD_CO_ALARM:
        if (clear) {
            a->co = co_quiet_state(a, cfg, cohb);
        }
        break;
    case HD_CO_HUSH:
        if (cohb >= cfg->co_no_hush_x100) {
            a->co = HD_CO_ALARM;            /* escalation overrides hush */
        } else if (clear) {
            a->co = co_quiet_state(a, cfg, cohb);
        } else if (a->co_hush_left == 0u) {
            a->co = HD_CO_ALARM;            /* hush expired, still high  */
        } else {
            --a->co_hush_left;
        }
        break;
    default:
        if (raise) {
            a->co = HD_CO_ALARM;
            a->memory |= HD_MEM_CO;
        } else {
            a->co = co_quiet_state(a, cfg, cohb);
        }
        break;
    }
}

/* -------------------------------------------------------------- fire --- */

static void fire_raise(hd_alarm_t *a, uint8_t cause)
{
    a->fire = HD_FS_ALARM;
    a->fire_cause = cause;
    a->fire_on_s = 0u;
    a->fire_clear_run = 0u;
    a->memory |= HD_MEM_FIRE;
}

static void fire_step(hd_alarm_t *a, const hd_alarm_cfg_t *cfg, uint8_t cause)
{
    uint8_t active = (uint8_t)(cause != HD_FIRE_NONE);
    a->fire_clear_run = active ? 0u : inc_u16(a->fire_clear_run);

    switch (a->fire) {
    case HD_FS_ALARM:
        a->fire_on_s = inc_u16(a->fire_on_s);
        if (!active && a->fire_on_s >= cfg->fire_min_on_s &&
            a->fire_clear_run >= cfg->fire_clear_s) {
            a->fire = HD_FS_IDLE;
        }
        break;
    case HD_FS_HUSH:
        if (active && !hd_fire_hush_allowed(cause)) {
            fire_raise(a, cause);           /* escalation overrides hush */
        } else if (a->fire_clear_run >= cfg->fire_clear_s) {
            a->fire = HD_FS_IDLE;
        } else if (a->fire_hush_left == 0u) {
            if (active) {
                fire_raise(a, cause);       /* hush expired, still burning */
            } else {
                a->fire = HD_FS_IDLE;
            }
        } else {
            --a->fire_hush_left;
        }
        break;
    default:
        if (active) {
            fire_raise(a, cause);
        }
        break;
    }
}

/* ------------------------------------------------------------ button --- */

static void button_step(hd_alarm_t *a, const hd_alarm_cfg_t *cfg,
                        const hd_alarm_in_t *in)
{
    a->ack = 0u;
    if (in->button == HD_BTN_LONG) {
        if (a->fire != HD_FS_ALARM && a->co != HD_CO_ALARM) {
            a->test_left = cfg->test_s;
        }
        return;
    }
    if (in->button != HD_BTN_SHORT) {
        return;
    }
    if (a->fire == HD_FS_ALARM) {
        if (hd_fire_hush_allowed(a->fire_cause) && hd_fire_hush_allowed(in->fire_cause)) {
            a->fire = HD_FS_HUSH;
            a->fire_hush_left = cfg->fire_hush_s;
        }
        return;
    }
    if (a->co == HD_CO_ALARM) {
        if (in->cohb_x100 < cfg->co_no_hush_x100 && !co_override_hit(cfg, in)) {
            a->co = HD_CO_HUSH;
            a->co_hush_left = cfg->co_hush_s;
        }
        return;
    }
    if (a->fire == HD_FS_IDLE && a->co != HD_CO_HUSH) {
        a->memory = 0u;
        a->ack = 1u;
    }
}

/* ------------------------------------------------------------ output --- */

static void arbitrate(hd_alarm_t *a, const hd_alarm_in_t *in)
{
    a->relay = (uint8_t)(a->co != HD_CO_IDLE);
    if (a->fire != HD_FS_IDLE) {
        a->relay = 0u;
    }
    if (a->fire == HD_FS_ALARM) {
        a->state = HD_STATE_FIRE_ALARM;
        a->sounder = HD_SND_FIRE_T3;
    } else if (a->co == HD_CO_ALARM) {
        a->state = HD_STATE_CO_ALARM;
        a->sounder = HD_SND_CO_T4;
    } else if (a->test_left > 0u) {
        a->state = HD_STATE_TEST;
        a->sounder = HD_SND_TEST;
    } else if (a->fire == HD_FS_HUSH) {
        a->state = HD_STATE_FIRE_HUSH;
        a->sounder = HD_SND_NONE;
    } else if (a->co == HD_CO_HUSH) {
        a->state = HD_STATE_CO_HUSH;
        a->sounder = HD_SND_NONE;
    } else if (a->co == HD_CO_WARN) {
        a->state = HD_STATE_CO_WARN;
        a->sounder = HD_SND_NONE;
    } else if (in->faults != 0u) {
        a->state = HD_STATE_TROUBLE;
        a->sounder = HD_SND_CHIRP;
    } else if (in->warming) {
        a->state = HD_STATE_WARMUP;
        a->sounder = HD_SND_NONE;
    } else {
        a->state = HD_STATE_NORMAL;
        a->sounder = HD_SND_NONE;
    }
}

void hd_alarm_step(hd_alarm_t *a, const hd_alarm_cfg_t *cfg, const hd_alarm_in_t *in)
{
    button_step(a, cfg, in);
    co_step(a, cfg, in);
    fire_step(a, cfg, in->fire_cause);
    if (a->test_left > 0u && in->button != HD_BTN_LONG) {
        --a->test_left;
    }
    arbitrate(a, in);
}

void hd_alarm_force_fire(hd_alarm_t *a, uint8_t cause)
{
    if (a->fire != HD_FS_ALARM) {
        fire_raise(a, cause);
    }
    a->state = HD_STATE_FIRE_ALARM;
    a->sounder = HD_SND_FIRE_T3;
    a->relay = 0u;
}

/* ------------------------------------------------------- annunciator --- */

/* Segment durations in ms, alternating on/off, starting with "on";
 * a 0 terminates the pattern (then it repeats). */
static const HD_ROM uint16_t k_patterns[HD_SND_COUNT][9] = {
    { 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u },                         /* none  */
    { 500u, 500u, 500u, 500u, 500u, 1500u, 0u, 0u, 0u },            /* T3    */
    { 100u, 100u, 100u, 100u, 100u, 100u, 100u, 5000u, 0u },        /* T4    */
    { 40u, 59960u, 0u, 0u, 0u, 0u, 0u, 0u, 0u },                    /* chirp */
    { 200u, 200u, 0u, 0u, 0u, 0u, 0u, 0u, 0u },                     /* test  */
};

void hd_annun_init(hd_annun_t *n)
{
    n->left_ms = 0u;
    n->clock_ms = 0u;
    n->pattern = HD_SND_NONE;
    n->seg = 0u;
}

static uint8_t sounder_tick(hd_annun_t *n, uint8_t pattern)
{
    if (pattern >= (uint8_t)HD_SND_COUNT) {
        pattern = HD_SND_NONE;
    }
    if (pattern != n->pattern) {
        n->pattern = pattern;
        n->seg = 0u;
        n->left_ms = k_patterns[pattern][0];
    }
    if (pattern == HD_SND_NONE) {
        return 0u;
    }
    while (n->left_ms == 0u) {
        ++n->seg;
        if (n->seg >= 9u || k_patterns[pattern][n->seg] == 0u) {
            n->seg = 0u;
        }
        n->left_ms = k_patterns[pattern][n->seg];
    }
    --n->left_ms;
    return (uint8_t)((n->seg & 1u) == 0u);   /* even segments are "on" */
}

static uint8_t led_bits(uint16_t t, uint8_t state)
{
    uint16_t phase = (uint16_t)(t & 2047u);
    switch (state) {
    case HD_STATE_FIRE_ALARM:
    case HD_STATE_CO_ALARM:
        return HD_OUT_RED;
    case HD_STATE_TEST:
        return (uint8_t)(HD_OUT_RED | HD_OUT_GREEN);
    case HD_STATE_FIRE_HUSH:
    case HD_STATE_CO_HUSH:
        return (phase < 1024u) ? HD_OUT_RED : 0u;
    case HD_STATE_CO_WARN:
        return (uint8_t)(HD_OUT_GREEN | ((phase < 150u) ? HD_OUT_RED : 0u));
    case HD_STATE_TROUBLE:
        return (phase < 150u) ? HD_OUT_RED : 0u;
    case HD_STATE_WARMUP:
        return (phase < 1024u) ? HD_OUT_GREEN : 0u;
    default:
        return (phase < 100u) ? HD_OUT_GREEN : 0u;
    }
}

uint8_t hd_annun_tick(hd_annun_t *n, uint8_t pattern, uint8_t state)
{
    uint8_t out = sounder_tick(n, pattern) ? HD_OUT_BUZZER : 0u;
    out |= led_bits(n->clock_ms, state);
    n->clock_ms = (uint16_t)(n->clock_ms + 1u);
    return out;
}
