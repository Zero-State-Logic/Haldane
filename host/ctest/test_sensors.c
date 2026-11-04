/*
 * HALDANE - host unit tests
 * Copyright (c) 2026 Faysal Ali Shah - MIT License
 *
 * test_sensors.c - ADC calibration, MQ-7 heater schedule, sensor health.
 */
#include "ht.h"
#include "bench.h"

/* ------------------------------------------------------- calibration --- */

static void test_mq7_ppm_round_trip(void)
{
    hd_mq_curve_t c;
    static const double ppm[] = { 20.0, 50.0, 70.0, 150.0, 400.0, 1000.0, 2000.0 };
    unsigned i;
    hd_mq7_curve_default(&c);
    for (i = 0; i < sizeof(ppm) / sizeof(ppm[0]); ++i) {
        uint16_t adc = bench_mq7_adc_q4(ppm[i]);
        /* the reference is the ppm the *quantised* ADC value encodes */
        double rs = (16384.0 - adc) / (double)adc;
        double ref = 99.042 * pow(rs, -1.518);
        double got = hd_mq_ppm_q4(&c, adc) / 16.0;
        HT_CHECK_NEAR(got / ref, 1.0, 0.005);
        HT_CHECK_NEAR(got / ppm[i], 1.0, 0.02);
    }
}

static void test_mq_calibration(void)
{
    hd_mq_curve_t c;
    uint16_t adc;
    int32_t cal;
    hd_mq7_curve_default(&c);
    /* a unit whose R0 is twice RL (RL/R0 = 0.5), in clean air */
    adc = bench_mq_adc_q4(27.5, 0.5);
    cal = hd_mq_calibrate_clean_air(&c, adc);
    HT_CHECK_NEAR(cal / 65536.0, -1.0, 0.01);
    c.log2_rl_r0_q16 = cal;
    HT_CHECK_NEAR(hd_mq_log2_ratio_q16(&c, adc) / 65536.0, log2(27.5), 0.01);
    /* once calibrated, 100 ppm on that unit reads ~100 ppm */
    adc = bench_mq_adc_q4(pow(100.0 / 99.042, 1.0 / -1.518), 0.5);
    HT_CHECK_NEAR(hd_mq_ppm_q4(&c, adc) / 16.0, 100.0, 3.0);
}

static void test_gas_index(void)
{
    hd_mq_curve_t c;
    hd_mq135_curve_default(&c);
    HT_CHECK_NEAR(hd_mq_gas_index_q12(&c, bench_mq135_adc_q4(0.0)) / 4096.0, 0.0, 0.01);
    HT_CHECK_NEAR(hd_mq_gas_index_q12(&c, bench_mq135_adc_q4(1.5)) / 4096.0, 1.5, 0.01);
    HT_CHECK(hd_mq_gas_index_q12(&c, 0u) < 0);              /* clamped, no crash */
    HT_CHECK(hd_mq_ppm_q4(&c, 16368u) == 65535u);           /* saturates      */
}

static void test_lm35(void)
{
    HT_CHECK_NEAR(hd_lm35_centi_c(bench_lm35_adc_q4(25.0), 5000u), 2500, 5);
    HT_CHECK_NEAR(hd_lm35_centi_c(bench_lm35_adc_q4(57.0), 5000u), 5700, 5);
    HT_CHECK_NEAR(hd_lm35_centi_c(bench_lm35_adc_q4(25.0), 5100u), 2550, 6);
    HT_CHECK_EQ(hd_lm35_centi_c(0u, 5000u), 0);
}

/* ------------------------------------------------------------- MQ-7 --- */

static void test_mq7_schedule(void)
{
    hd_mq7_cfg_t cfg;
    hd_mq7_t m;
    int s;
    int high_done_at = -1;
    int sample_at = -1;
    hd_mq7_cfg_default(&cfg);
    hd_mq7_init(&m);
    HT_CHECK_EQ(hd_mq7_heater_duty(&m, &cfg), 255);
    for (s = 1; s <= 150; ++s) {
        uint8_t ev = hd_mq7_second(&m, &cfg, (uint16_t)(1000 + s));
        if (ev & HD_MQ7_EV_HIGH_DONE) {
            high_done_at = s;
            HT_CHECK_EQ(hd_mq7_heater_duty(&m, &cfg), 20);
        }
        if (ev & HD_MQ7_EV_SAMPLE) {
            sample_at = s;
        }
    }
    HT_CHECK_EQ(high_done_at, 60);
    HT_CHECK_EQ(sample_at, 150);
    /* sample = mean of the last 5 low-phase seconds (146..150) */
    HT_CHECK_EQ(m.sample_q4, 1148);
    HT_CHECK_EQ(m.high_mean_q4, (uint16_t)(1000 + 30));   /* mean of 1001..1060 */
    HT_CHECK_EQ(hd_mq7_heater_duty(&m, &cfg), 255);
}

static void test_mq7_warmup(void)
{
    hd_mq7_cfg_t cfg;
    hd_mq7_t m;
    int s;
    hd_mq7_cfg_default(&cfg);
    hd_mq7_init(&m);
    for (s = 0; s < 300; ++s) {
        (void)hd_mq7_second(&m, &cfg, 500u);
    }
    HT_CHECK_EQ(hd_mq7_is_warm(&m, &cfg), 0);     /* two cycles discarded */
    for (s = 0; s < 150; ++s) {
        (void)hd_mq7_second(&m, &cfg, 500u);
    }
    HT_CHECK_EQ(hd_mq7_is_warm(&m, &cfg), 1);
}

/* ----------------------------------------------------------- health --- */

static hd_health_cfg_t health_cfg(void)
{
    hd_health_cfg_t c;
    c.open_below_q4 = 64u;
    c.short_above_q4 = 16240u;
    c.stuck_s = 600u;
    c.debounce_s = 3u;
    c.recover_s = 10u;
    return c;
}

static void test_health_open_short(void)
{
    hd_health_cfg_t c = health_cfg();
    hd_health_ch_t h;
    int s;
    hd_health_init(&h);
    for (s = 0; s < 100; ++s) {
        HT_CHECK_EQ(hd_health_update(&h, &c, 4000u, 249u, 251u), 0);
    }
    HT_CHECK_EQ(hd_health_update(&h, &c, 10u, 0u, 1u), 0);
    HT_CHECK_EQ(hd_health_update(&h, &c, 10u, 0u, 1u), 0);
    HT_CHECK_EQ(hd_health_update(&h, &c, 10u, 0u, 1u), HD_HF_OPEN);
    for (s = 0; s < 9; ++s) {
        HT_CHECK_EQ(hd_health_update(&h, &c, 4000u, 249u, 251u), HD_HF_OPEN);
    }
    HT_CHECK_EQ(hd_health_update(&h, &c, 4000u, 249u, 251u), 0);
    for (s = 0; s < 3; ++s) {
        (void)hd_health_update(&h, &c, 16368u, 1022u, 1023u);
    }
    HT_CHECK_EQ(h.faults, HD_HF_SHORT);
}

static void test_health_stuck(void)
{
    hd_health_cfg_t c = health_cfg();
    hd_health_ch_t h;
    int s;
    hd_health_init(&h);
    for (s = 0; s < 600; ++s) {
        (void)hd_health_update(&h, &c, 4000u, 250u, 250u);
    }
    HT_CHECK_EQ(h.faults, 0);                       /* debounce pending */
    for (s = 0; s < 2; ++s) {
        (void)hd_health_update(&h, &c, 4000u, 250u, 250u);
    }
    HT_CHECK_EQ(h.faults, HD_HF_STUCK);
    for (s = 0; s < 10; ++s) {
        (void)hd_health_update(&h, &c, 4000u, 249u, 251u);
    }
    HT_CHECK_EQ(h.faults, 0);
}

static void test_health_disabled_checks(void)
{
    hd_health_cfg_t c = health_cfg();
    hd_health_ch_t h;
    int s;
    c.open_below_q4 = 0u;
    c.short_above_q4 = 0u;
    c.stuck_s = 0u;
    hd_health_init(&h);
    for (s = 0; s < 1000; ++s) {
        HT_CHECK_EQ(hd_health_update(&h, &c, 0u, 0u, 0u), 0);
    }
}

void test_sensors_all(void)
{
    ht_run("calib.mq7_ppm_round_trip", test_mq7_ppm_round_trip);
    ht_run("calib.clean_air_calibration", test_mq_calibration);
    ht_run("calib.gas_index", test_gas_index);
    ht_run("calib.lm35", test_lm35);
    ht_run("mq7.heater_schedule", test_mq7_schedule);
    ht_run("mq7.warmup", test_mq7_warmup);
    ht_run("health.open_short", test_health_open_short);
    ht_run("health.stuck", test_health_stuck);
    ht_run("health.disabled_checks", test_health_disabled_checks);
}
