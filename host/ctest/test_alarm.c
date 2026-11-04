/*
 * HALDANE - host unit tests
 * Copyright (c) 2026 Faysal Ali Shah - MIT License
 *
 * test_alarm.c - alarm state machine and annunciator.
 */
#include "ht.h"
#include "bench.h"

static hd_alarm_in_t idle_in(void)
{
    hd_alarm_in_t in;
    in.cohb_x100 = 50u;
    in.co_ppm_q4 = 0u;
    in.faults = 0u;
    in.co_valid = 1u;
    in.warming = 0u;
    in.fire_cause = HD_FIRE_NONE;
    in.button = HD_BTN_NONE;
    return in;
}

static void setup(hd_alarm_t *a, hd_alarm_cfg_t *cfg)
{
    hd_alarm_cfg_default(cfg);
    hd_alarm_init(a);
}

static void test_co_raise_clear_hysteresis(void)
{
    hd_alarm_cfg_t cfg;
    hd_alarm_t a;
    hd_alarm_in_t in = idle_in();
    setup(&a, &cfg);
    in.cohb_x100 = 549u;
    hd_alarm_step(&a, &cfg, &in);
    HT_CHECK_EQ(a.state, HD_STATE_CO_WARN);          /* >= 3 %: pre-alarm  */
    HT_CHECK_EQ(a.relay, 1);
    HT_CHECK_EQ(a.sounder, HD_SND_NONE);
    in.cohb_x100 = 550u;
    hd_alarm_step(&a, &cfg, &in);
    HT_CHECK_EQ(a.state, HD_STATE_CO_ALARM);
    HT_CHECK_EQ(a.sounder, HD_SND_CO_T4);
    HT_CHECK_EQ(a.memory & HD_MEM_CO, HD_MEM_CO);
    in.cohb_x100 = 510u;                             /* inside hysteresis */
    hd_alarm_step(&a, &cfg, &in);
    HT_CHECK_EQ(a.state, HD_STATE_CO_ALARM);
    in.cohb_x100 = 499u;
    hd_alarm_step(&a, &cfg, &in);
    HT_CHECK_EQ(a.state, HD_STATE_CO_WARN);
    in.cohb_x100 = 100u;
    hd_alarm_step(&a, &cfg, &in);
    HT_CHECK_EQ(a.state, HD_STATE_NORMAL);
    HT_CHECK_EQ(a.relay, 0);
    HT_CHECK_EQ(a.memory & HD_MEM_CO, HD_MEM_CO);    /* latched memory */
}

static void test_fire_beats_co(void)
{
    hd_alarm_cfg_t cfg;
    hd_alarm_t a;
    hd_alarm_in_t in = idle_in();
    setup(&a, &cfg);
    in.cohb_x100 = 800u;
    in.fire_cause = HD_FIRE_SMOULDERING;
    hd_alarm_step(&a, &cfg, &in);
    HT_CHECK_EQ(a.state, HD_STATE_FIRE_ALARM);
    HT_CHECK_EQ(a.sounder, HD_SND_FIRE_T3);
    HT_CHECK_EQ(a.relay, 0);                         /* no fan in a fire */
    HT_CHECK_EQ(a.co, HD_CO_ALARM);                  /* still tracked    */
}

static void test_fire_latching(void)
{
    hd_alarm_cfg_t cfg;
    hd_alarm_t a;
    hd_alarm_in_t in = idle_in();
    int s;
    setup(&a, &cfg);
    in.fire_cause = HD_FIRE_FLAMING;
    hd_alarm_step(&a, &cfg, &in);
    in.fire_cause = HD_FIRE_NONE;
    for (s = 0; s < 59; ++s) {
        hd_alarm_step(&a, &cfg, &in);
        HT_CHECK_EQ(a.state, HD_STATE_FIRE_ALARM);
    }
    hd_alarm_step(&a, &cfg, &in);
    HT_CHECK_EQ(a.state, HD_STATE_NORMAL);
    HT_CHECK_EQ(a.memory & HD_MEM_FIRE, HD_MEM_FIRE);
    in.button = HD_BTN_SHORT;
    hd_alarm_step(&a, &cfg, &in);
    HT_CHECK_EQ(a.memory, 0);
    HT_CHECK_EQ(a.ack, 1);
}

static void test_fire_hush_rules(void)
{
    hd_alarm_cfg_t cfg;
    hd_alarm_t a;
    hd_alarm_in_t in = idle_in();
    setup(&a, &cfg);
    in.fire_cause = HD_FIRE_SMOKE_HEAT;
    hd_alarm_step(&a, &cfg, &in);
    in.button = HD_BTN_SHORT;
    hd_alarm_step(&a, &cfg, &in);
    HT_CHECK_EQ(a.state, HD_STATE_FIRE_HUSH);
    HT_CHECK_EQ(a.sounder, HD_SND_NONE);
    in.button = HD_BTN_NONE;
    in.fire_cause = HD_FIRE_HEAT_FIXED;              /* escalation */
    hd_alarm_step(&a, &cfg, &in);
    HT_CHECK_EQ(a.state, HD_STATE_FIRE_ALARM);
    in.button = HD_BTN_SHORT;                        /* refused now */
    hd_alarm_step(&a, &cfg, &in);
    HT_CHECK_EQ(a.state, HD_STATE_FIRE_ALARM);
}

static void test_fire_hush_expires(void)
{
    hd_alarm_cfg_t cfg;
    hd_alarm_t a;
    hd_alarm_in_t in = idle_in();
    int s;
    setup(&a, &cfg);
    in.fire_cause = HD_FIRE_FLAMING;
    hd_alarm_step(&a, &cfg, &in);
    in.button = HD_BTN_SHORT;
    hd_alarm_step(&a, &cfg, &in);
    in.button = HD_BTN_NONE;
    for (s = 0; s < 599; ++s) {
        hd_alarm_step(&a, &cfg, &in);
    }
    HT_CHECK_EQ(a.state, HD_STATE_FIRE_HUSH);
    hd_alarm_step(&a, &cfg, &in);
    HT_CHECK_EQ(a.state, HD_STATE_FIRE_ALARM);
}

static void test_co_hush_rules(void)
{
    hd_alarm_cfg_t cfg;
    hd_alarm_t a;
    hd_alarm_in_t in = idle_in();
    int s;
    setup(&a, &cfg);
    in.cohb_x100 = 700u;
    hd_alarm_step(&a, &cfg, &in);
    in.button = HD_BTN_SHORT;
    hd_alarm_step(&a, &cfg, &in);
    HT_CHECK_EQ(a.state, HD_STATE_CO_HUSH);
    HT_CHECK_EQ(a.relay, 1);                         /* fan keeps running */
    in.button = HD_BTN_NONE;
    for (s = 0; s < 299; ++s) {
        hd_alarm_step(&a, &cfg, &in);
    }
    HT_CHECK_EQ(a.state, HD_STATE_CO_HUSH);
    hd_alarm_step(&a, &cfg, &in);
    HT_CHECK_EQ(a.state, HD_STATE_CO_ALARM);          /* expired, still high */
    in.cohb_x100 = 1000u;                            /* the 10 % line */
    in.button = HD_BTN_SHORT;
    hd_alarm_step(&a, &cfg, &in);
    HT_CHECK_EQ(a.state, HD_STATE_CO_ALARM);          /* refused */
    in.cohb_x100 = 900u;
    hd_alarm_step(&a, &cfg, &in);
    HT_CHECK_EQ(a.state, HD_STATE_CO_HUSH);
    in.button = HD_BTN_NONE;
    in.cohb_x100 = 1001u;                            /* escalation */
    hd_alarm_step(&a, &cfg, &in);
    HT_CHECK_EQ(a.state, HD_STATE_CO_ALARM);
}

static void test_override_ppm(void)
{
    hd_config_t cfg;
    hd_alarm_t a;
    hd_alarm_in_t in = idle_in();
    bench_config(&cfg, HD_PROFILE_SENSITIVE);
    hd_alarm_init(&a);
    in.co_ppm_q4 = 300u * 16u;
    hd_alarm_step(&a, &cfg.alarm, &in);
    HT_CHECK_EQ(a.state, HD_STATE_CO_ALARM);
    in.button = HD_BTN_SHORT;                        /* cannot hush 300 ppm */
    hd_alarm_step(&a, &cfg.alarm, &in);
    HT_CHECK_EQ(a.state, HD_STATE_CO_ALARM);
    in.co_valid = 0u;                                /* override needs a valid reading */
    in.co_ppm_q4 = 0u;
    hd_alarm_init(&a);
    in.button = HD_BTN_NONE;
    hd_alarm_step(&a, &cfg.alarm, &in);
    HT_CHECK_EQ(a.state, HD_STATE_NORMAL);
}

static void test_trouble_warmup_test(void)
{
    hd_alarm_cfg_t cfg;
    hd_alarm_t a;
    hd_alarm_in_t in = idle_in();
    int s;
    setup(&a, &cfg);
    in.warming = 1u;
    hd_alarm_step(&a, &cfg, &in);
    HT_CHECK_EQ(a.state, HD_STATE_WARMUP);
    in.faults = HD_FAULT_MQ7_HEATER;
    hd_alarm_step(&a, &cfg, &in);
    HT_CHECK_EQ(a.state, HD_STATE_TROUBLE);
    HT_CHECK_EQ(a.sounder, HD_SND_CHIRP);
    in.button = HD_BTN_LONG;
    hd_alarm_step(&a, &cfg, &in);
    HT_CHECK_EQ(a.state, HD_STATE_TEST);
    in.button = HD_BTN_NONE;
    for (s = 0; s < 6; ++s) {
        hd_alarm_step(&a, &cfg, &in);
    }
    HT_CHECK_EQ(a.state, HD_STATE_TROUBLE);
    in.cohb_x100 = 800u;                             /* no test during alarm */
    hd_alarm_step(&a, &cfg, &in);
    in.button = HD_BTN_LONG;
    hd_alarm_step(&a, &cfg, &in);
    HT_CHECK_EQ(a.state, HD_STATE_CO_ALARM);
}

static void test_force_fire(void)
{
    hd_alarm_cfg_t cfg;
    hd_alarm_t a;
    hd_alarm_in_t in = idle_in();
    setup(&a, &cfg);
    in.cohb_x100 = 600u;
    hd_alarm_step(&a, &cfg, &in);
    hd_alarm_force_fire(&a, HD_FIRE_FLAME_FAST);
    HT_CHECK_EQ(a.state, HD_STATE_FIRE_ALARM);
    HT_CHECK_EQ(a.relay, 0);
    HT_CHECK_EQ(a.fire_cause, HD_FIRE_FLAME_FAST);
}

/* Measure buzzer on-time and period of a pattern over n ms. */
static void measure(uint8_t pattern, int n_ms, int *on_ms, int *first_off_ms)
{
    hd_annun_t an;
    int i;
    *on_ms = 0;
    *first_off_ms = -1;
    hd_annun_init(&an);
    for (i = 0; i < n_ms; ++i) {
        uint8_t out = hd_annun_tick(&an, pattern, HD_STATE_FIRE_ALARM);
        if (out & HD_OUT_BUZZER) {
            ++*on_ms;
        } else if (*first_off_ms < 0) {
            *first_off_ms = i;
        }
    }
}

static void test_annunciator_patterns(void)
{
    int on;
    int first_off;
    measure(HD_SND_FIRE_T3, 4000, &on, &first_off);    /* one T3 period */
    HT_CHECK_EQ(on, 1500);
    HT_CHECK_EQ(first_off, 500);
    measure(HD_SND_FIRE_T3, 8000, &on, &first_off);
    HT_CHECK_EQ(on, 3000);
    measure(HD_SND_CO_T4, 5700, &on, &first_off);      /* one T4 period */
    HT_CHECK_EQ(on, 400);
    HT_CHECK_EQ(first_off, 100);
    measure(HD_SND_CHIRP, 60000, &on, &first_off);
    HT_CHECK_EQ(on, 40);
    measure(HD_SND_NONE, 5000, &on, &first_off);
    HT_CHECK_EQ(on, 0);
}

static void test_annunciator_leds(void)
{
    hd_annun_t an;
    int i;
    int red = 0;
    int green = 0;
    hd_annun_init(&an);
    for (i = 0; i < 2048; ++i) {
        uint8_t out = hd_annun_tick(&an, HD_SND_NONE, HD_STATE_NORMAL);
        red += (out & HD_OUT_RED) ? 1 : 0;
        green += (out & HD_OUT_GREEN) ? 1 : 0;
    }
    HT_CHECK_EQ(red, 0);
    HT_CHECK_EQ(green, 100);                         /* heartbeat */
    hd_annun_init(&an);
    HT_CHECK((hd_annun_tick(&an, HD_SND_CO_T4, HD_STATE_CO_ALARM) & HD_OUT_RED) != 0);
}

void test_alarm_all(void)
{
    ht_run("alarm.co_raise_clear_hysteresis", test_co_raise_clear_hysteresis);
    ht_run("alarm.fire_beats_co", test_fire_beats_co);
    ht_run("alarm.fire_latching", test_fire_latching);
    ht_run("alarm.fire_hush_rules", test_fire_hush_rules);
    ht_run("alarm.fire_hush_expires", test_fire_hush_expires);
    ht_run("alarm.co_hush_rules", test_co_hush_rules);
    ht_run("alarm.override_ppm", test_override_ppm);
    ht_run("alarm.trouble_warmup_test", test_trouble_warmup_test);
    ht_run("alarm.force_fire", test_force_fire);
    ht_run("annun.patterns", test_annunciator_patterns);
    ht_run("annun.leds", test_annunciator_leds);
}
