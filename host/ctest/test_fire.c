/*
 * HALDANE - host unit tests
 * Copyright (c) 2026 Faysal Ali Shah - MIT License
 *
 * test_fire.c - flicker DSP, CUSUM evidence and fusion rules.
 */
#include "ht.h"
#include "bench.h"

#define PI 3.14159265358979

/* Feed one second (100 frames) of a sinusoidally modulated IR level. */
static void feed_flame_second(hd_fire_t *f, const hd_fire_cfg_t *cfg, long sec,
                              double level, double depth, double freq_hz)
{
    int i;
    for (i = 0; i < 100; ++i) {
        double t = (double)sec + i / 100.0;
        double v = level * (1.0 + depth * sin(2.0 * PI * freq_hz * t));
        if (v < 0.0) {
            v = 0.0;
        }
        if (v > 10230.0) {
            v = 10230.0;
        }
        hd_fire_flame_frame(f, cfg, (uint16_t)v);
    }
}

static hd_fire_in_t quiet_in(double temp_c)
{
    hd_fire_in_t in;
    in.temp_cc = (int32_t)(temp_c * 100.0);
    in.gas_q12 = 0;
    in.co_ppm_q4 = 0u;
    in.co_new = 0u;
    in.valid = HD_FIRE_V_TEMP | HD_FIRE_V_GAS | HD_FIRE_V_CO | HD_FIRE_V_FLAME;
    return in;
}

static uint8_t rel_after(double level, double depth, double freq)
{
    hd_fire_cfg_t cfg;
    hd_fire_t f;
    hd_fire_in_t in = quiet_in(22.0);
    long s;
    hd_fire_cfg_default(&cfg);
    hd_fire_init(&f);
    for (s = 0; s < 10; ++s) {
        feed_flame_second(&f, &cfg, s, level, depth, freq);
        (void)hd_fire_second(&f, &cfg, &in);
    }
    return f.rel_q8;
}

static void test_flicker_band(void)
{
    uint8_t flame = rel_after(4000.0, 0.3, 8.0);
    uint8_t dc = rel_after(4000.0, 0.0, 8.0);
    uint8_t slow = rel_after(4000.0, 0.3, 0.05);    /* clouds over the sun */
    uint8_t fast = rel_after(4000.0, 0.3, 45.0);    /* above the band      */
    printf("    relative flicker (Q8): 8 Hz %u, DC %u, 0.05 Hz %u, 45 Hz %u\n",
           flame, dc, slow, fast);
    HT_CHECK(flame >= 30u);
    HT_CHECK(dc <= 1u);
    HT_CHECK(slow <= 3u);
    HT_CHECK((unsigned)fast * 3u < (unsigned)flame * 2u);   /* first-order roll-off */
}

/* Seconds until a non-zero cause, or -1. */
static long run_until_fire(hd_fire_t *f, const hd_fire_cfg_t *cfg, long max_s,
                           double level, double depth, double temp_rate_c_min,
                           double gas_rate_min, double co_rate_ppm_min)
{
    long s;
    for (s = 0; s < max_s; ++s) {
        hd_fire_in_t in = quiet_in(22.0 + temp_rate_c_min * s / 60.0);
        in.gas_q12 = (int32_t)(4096.0 * gas_rate_min * s / 60.0);
        if (s % 150 == 149) {
            in.co_new = 1u;
            in.co_ppm_q4 = (uint16_t)(16.0 * co_rate_ppm_min * s / 60.0);
        }
        feed_flame_second(f, cfg, s, level, depth, 7.0);
        if (hd_fire_second(f, cfg, &in) != HD_FIRE_NONE) {
            return s;
        }
    }
    return -1;
}

static void test_flaming_needs_corroboration(void)
{
    hd_fire_cfg_t cfg;
    hd_fire_t f;
    long t;
    hd_fire_cfg_default(&cfg);
    hd_fire_init(&f);
    t = run_until_fire(&f, &cfg, 120, 3000.0, 0.3, 0.0, 0.0, 0.0);
    HT_CHECK_EQ(t, -1);                              /* flicker alone: no  */
    hd_fire_init(&f);
    t = run_until_fire(&f, &cfg, 120, 3000.0, 0.3, 4.0, 0.0, 0.0);
    HT_CHECK(t > 0 && t < 60);
    HT_CHECK_EQ(f.cause, HD_FIRE_FLAMING);
}

static void test_smouldering(void)
{
    hd_fire_cfg_t cfg;
    hd_fire_t f;
    long t;
    hd_fire_cfg_default(&cfg);
    hd_fire_init(&f);
    t = run_until_fire(&f, &cfg, 3600, 200.0, 0.0, 0.0, 0.15, 6.0);
    HT_CHECK(t > 0);
    HT_CHECK_EQ(f.cause, HD_FIRE_SMOULDERING);
}

static void test_slow_drift_is_tolerated(void)
{
    hd_fire_cfg_t cfg;
    hd_fire_t f;
    hd_fire_cfg_default(&cfg);
    hd_fire_init(&f);
    /* 0.5 degC/min warming, 0.03 doublings/min gas drift, 1 ppm/min CO */
    HT_CHECK_EQ(run_until_fire(&f, &cfg, 3600, 200.0, 0.0, 0.5, 0.03, 1.0), -1);
}

static void test_fixed_and_ror_backups(void)
{
    hd_fire_cfg_t cfg;
    hd_fire_t f;
    long s;
    uint8_t cause = HD_FIRE_NONE;
    hd_fire_cfg_default(&cfg);
    hd_fire_init(&f);
    for (s = 0; s < 10 && cause == HD_FIRE_NONE; ++s) {
        hd_fire_in_t in = quiet_in(58.0);
        in.valid = HD_FIRE_V_TEMP;
        cause = hd_fire_second(&f, &cfg, &in);
    }
    HT_CHECK_EQ(cause, HD_FIRE_HEAT_FIXED);
    HT_CHECK_EQ(s, 5);
    hd_fire_init(&f);
    cause = HD_FIRE_NONE;
    for (s = 0; s < 300 && cause == HD_FIRE_NONE; ++s) {
        hd_fire_in_t in = quiet_in(22.0 + 12.0 * s / 60.0);   /* 12 degC/min */
        in.valid = HD_FIRE_V_TEMP;
        cause = hd_fire_second(&f, &cfg, &in);
    }
    HT_CHECK_EQ(cause, HD_FIRE_HEAT_ROR);
}

static void test_fast_path(void)
{
    hd_fire_cfg_t cfg;
    hd_fire_t f;
    int i;
    hd_fire_cfg_default(&cfg);
    hd_fire_init(&f);
    for (i = 0; i < 3; ++i) {
        feed_flame_second(&f, &cfg, i, 7000.0, 0.4, 6.0);
    }
    HT_CHECK_EQ(f.fast_fire, 0);                     /* no INT0 edge yet */
    hd_fire_flame_edge(&f, &cfg);
    for (i = 0; i < 160 && !f.fast_fire; ++i) {
        double v = 7000.0 * (1.0 + 0.4 * sin(2.0 * PI * 6.0 * (3.0 + i / 100.0)));
        hd_fire_flame_frame(&f, &cfg, (uint16_t)v);
    }
    HT_CHECK_EQ(f.fast_fire, 1);
    HT_CHECK(i >= 150 && i <= 152);
    /* steady sunlight-like IR with an edge: no fast fire */
    hd_fire_init(&f);
    for (i = 0; i < 3; ++i) {
        feed_flame_second(&f, &cfg, i, 9000.0, 0.0, 6.0);
    }
    hd_fire_flame_edge(&f, &cfg);
    feed_flame_second(&f, &cfg, 3, 9000.0, 0.0, 6.0);
    feed_flame_second(&f, &cfg, 4, 9000.0, 0.0, 6.0);
    HT_CHECK_EQ(f.fast_fire, 0);
}

static void test_invalid_channels_give_no_evidence(void)
{
    hd_fire_cfg_t cfg;
    hd_fire_t f;
    long s;
    uint8_t cause = HD_FIRE_NONE;
    hd_fire_cfg_default(&cfg);
    hd_fire_init(&f);
    for (s = 0; s < 120; ++s) {
        hd_fire_in_t in = quiet_in(80.0);            /* would be fixed heat */
        in.valid = HD_FIRE_V_GAS | HD_FIRE_V_CO;     /* ...but LM35 faulty  */
        feed_flame_second(&f, &cfg, s, 8000.0, 0.4, 6.0);
        in.valid &= (uint8_t)(0xFFu ^ HD_FIRE_V_FLAME);
        cause |= hd_fire_second(&f, &cfg, &in);
    }
    HT_CHECK_EQ(cause, HD_FIRE_NONE);
    HT_CHECK_EQ(f.s_temp, 0);
    HT_CHECK_EQ(f.s_flk, 0);
}

static void test_hush_table(void)
{
    HT_CHECK_EQ(hd_fire_hush_allowed(HD_FIRE_FLAMING), 1);
    HT_CHECK_EQ(hd_fire_hush_allowed(HD_FIRE_SMOULDERING), 1);
    HT_CHECK_EQ(hd_fire_hush_allowed(HD_FIRE_SMOKE_HEAT), 1);
    HT_CHECK_EQ(hd_fire_hush_allowed(HD_FIRE_HEAT_FIXED), 0);
    HT_CHECK_EQ(hd_fire_hush_allowed(HD_FIRE_HEAT_ROR), 0);
    HT_CHECK_EQ(hd_fire_hush_allowed(HD_FIRE_FLAME_FAST), 0);
}

void test_fire_all(void)
{
    ht_run("fire.flicker_band", test_flicker_band);
    ht_run("fire.flaming_needs_corroboration", test_flaming_needs_corroboration);
    ht_run("fire.smouldering", test_smouldering);
    ht_run("fire.slow_drift_tolerated", test_slow_drift_is_tolerated);
    ht_run("fire.fixed_and_ror_backups", test_fixed_and_ror_backups);
    ht_run("fire.fast_path", test_fast_path);
    ht_run("fire.invalid_channels_no_evidence", test_invalid_channels_give_no_evidence);
    ht_run("fire.hush_table", test_hush_table);
}
