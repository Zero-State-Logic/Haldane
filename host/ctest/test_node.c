/*
 * HALDANE - host unit tests
 * Copyright (c) 2026 Faysal Ali Shah - MIT License
 *
 * test_node.c - the integrated node, driven the way the firmware drives it.
 */
#include "ht.h"
#include "bench.h"

#define PI 3.14159265358979

static void dark_frames(hd_node_t *n, const hd_config_t *cfg)
{
    int i;
    for (i = 0; i < 100; ++i) {
        hd_node_flame_frame(n, cfg, 10000u);          /* inverted: ir 230 */
    }
}

static void run_seconds(hd_node_t *n, const hd_config_t *cfg, long count,
                        double co_ppm, double temp_c, double gas)
{
    long s;
    for (s = 0; s < count; ++s) {
        hd_node_in_t in;
        bench_fill(&in, co_ppm, temp_c, gas);
        dark_frames(n, cfg);
        hd_node_second(n, cfg, &in);
    }
}

static void test_clean_air_is_quiet(void)
{
    hd_config_t cfg;
    hd_node_t n;
    bench_config(&cfg, HD_PROFILE_UL2034);
    HT_CHECK_EQ(hd_node_init(&n, &cfg), HD_OK);
    run_seconds(&n, &cfg, 100, 0.0, 22.0, 0.0);
    HT_CHECK_EQ(n.alarm.state, HD_STATE_WARMUP);
    run_seconds(&n, &cfg, 3600, 0.0, 22.0, 0.0);
    HT_CHECK_EQ(n.alarm.state, HD_STATE_NORMAL);
    HT_CHECK_EQ(n.faults, 0);
    HT_CHECK_EQ(n.co_valid, 1);
    HT_CHECK(n.cohb_x100 < 30u);
    HT_CHECK_NEAR(n.temp_cc, 2200, 10);
    HT_CHECK_EQ(hd_node_heater_duty(&n, &cfg) == 255 || hd_node_heater_duty(&n, &cfg) == 20, 1);
}

static void test_co_400ppm_alarm_in_window(void)
{
    hd_config_t cfg;
    hd_node_t n;
    long s;
    long alarm_s = -1;
    bench_config(&cfg, HD_PROFILE_UL2034);
    (void)hd_node_init(&n, &cfg);
    run_seconds(&n, &cfg, 1200, 0.0, 22.0, 0.0);
    for (s = 1; s <= 30L * 60L && alarm_s < 0; ++s) {
        run_seconds(&n, &cfg, 1, 400.0, 22.0, 0.0);
        if (n.alarm.state == HD_STATE_CO_ALARM) {
            alarm_s = s;
        }
    }
    printf("    node, 400 ppm step: CO alarm after %.1f min\n", alarm_s / 60.0);
    HT_CHECK(alarm_s >= 4L * 60L && alarm_s <= 15L * 60L);
    HT_CHECK_EQ(n.alarm.relay, 1);
    HT_CHECK_EQ(n.alarm.sounder, HD_SND_CO_T4);
}

static void test_mq7_open_raises_trouble(void)
{
    hd_config_t cfg;
    hd_node_t n;
    long s;
    bench_config(&cfg, HD_PROFILE_UL2034);
    (void)hd_node_init(&n, &cfg);
    run_seconds(&n, &cfg, 600, 0.0, 22.0, 0.0);
    for (s = 0; s < 5; ++s) {
        hd_node_in_t in;
        bench_fill(&in, 0.0, 22.0, 0.0);
        in.mean_q4[HD_CH_MQ7] = 0u;
        in.raw_min[HD_CH_MQ7] = 0u;
        in.raw_max[HD_CH_MQ7] = 0u;
        hd_node_second(&n, &cfg, &in);
    }
    HT_CHECK((n.faults & (HD_HF_OPEN << HD_FAULT_SHIFT_MQ7)) != 0u);
    HT_CHECK_EQ(n.co_valid, 0);
    HT_CHECK_EQ(n.alarm.state, HD_STATE_TROUBLE);
}

static void test_heater_fault(void)
{
    hd_config_t cfg;
    hd_node_t n;
    long s;
    bench_config(&cfg, HD_PROFILE_UL2034);
    (void)hd_node_init(&n, &cfg);
    /* a cold sensor (heater open) reads ~5 counts in both phases; the
     * open-circuit check is disabled here to isolate the heater check */
    cfg.health[HD_CH_MQ7].open_below_q4 = 0u;
    for (s = 0; s < 200; ++s) {
        hd_node_in_t in;
        bench_fill(&in, 0.0, 22.0, 0.0);
        in.mean_q4[HD_CH_MQ7] = 80u;
        in.raw_min[HD_CH_MQ7] = 4u;
        in.raw_max[HD_CH_MQ7] = 6u;
        dark_frames(&n, &cfg);
        hd_node_second(&n, &cfg, &in);
    }
    HT_CHECK((n.faults & HD_FAULT_MQ7_HEATER) != 0u);
    HT_CHECK_EQ(n.co_valid, 0);
}

static void test_flaming_fire_alarm(void)
{
    hd_config_t cfg;
    hd_node_t n;
    long s;
    long alarm_s = -1;
    bench_config(&cfg, HD_PROFILE_UL2034);
    (void)hd_node_init(&n, &cfg);
    run_seconds(&n, &cfg, 600, 0.0, 22.0, 0.0);
    for (s = 0; s < 300 && alarm_s < 0; ++s) {
        hd_node_in_t in;
        int i;
        bench_fill(&in, 0.0, 22.0 + 5.0 * s / 60.0, 0.02 * s / 60.0);
        for (i = 0; i < 100; ++i) {
            double t = (double)s + i / 100.0;
            double ir = 3000.0 * (1.0 + 0.3 * sin(2.0 * PI * 6.0 * t));
            hd_node_flame_frame(&n, &cfg, (uint16_t)(10230.0 - ir));
        }
        hd_node_second(&n, &cfg, &in);
        if (n.alarm.state == HD_STATE_FIRE_ALARM) {
            alarm_s = s;
        }
    }
    HT_CHECK(alarm_s > 0 && alarm_s < 90);
    HT_CHECK_EQ(n.alarm.relay, 0);
    HT_CHECK_EQ(n.alarm.sounder, HD_SND_FIRE_T3);
}

static void test_fast_path_through_node(void)
{
    hd_config_t cfg;
    hd_node_t n;
    int i;
    bench_config(&cfg, HD_PROFILE_UL2034);
    (void)hd_node_init(&n, &cfg);
    run_seconds(&n, &cfg, 600, 0.0, 22.0, 0.0);
    hd_node_flame_edge(&n, &cfg);
    for (i = 0; i < 300 && n.alarm.state != HD_STATE_FIRE_ALARM; ++i) {
        double ir = 7500.0 * (1.0 + 0.35 * sin(2.0 * PI * 5.0 * i / 100.0));
        hd_node_flame_frame(&n, &cfg, (uint16_t)(10230.0 - ir));
    }
    HT_CHECK_EQ(n.alarm.state, HD_STATE_FIRE_ALARM);
    HT_CHECK_EQ(n.alarm.fire_cause, HD_FIRE_FLAME_FAST);
    HT_CHECK(i < 250);                               /* well under 2.5 s */
}

static void test_reset_fault_ack(void)
{
    hd_config_t cfg;
    hd_node_t n;
    hd_node_in_t in;
    bench_config(&cfg, HD_PROFILE_UL2034);
    (void)hd_node_init(&n, &cfg);
    hd_node_note_reset(&n, HD_FAULT_RESET_WDT);
    run_seconds(&n, &cfg, 600, 0.0, 22.0, 0.0);
    HT_CHECK((n.faults & HD_FAULT_RESET_WDT) != 0u);
    HT_CHECK_EQ(n.alarm.state, HD_STATE_TROUBLE);
    bench_fill(&in, 0.0, 22.0, 0.0);
    in.button = HD_BTN_SHORT;
    hd_node_second(&n, &cfg, &in);
    run_seconds(&n, &cfg, 1, 0.0, 22.0, 0.0);
    HT_CHECK_EQ(n.faults, 0);
    HT_CHECK_EQ(n.alarm.state, HD_STATE_NORMAL);
}

static void test_uncalibrated_is_trouble(void)
{
    hd_config_t cfg;
    hd_node_t n;
    (void)hd_config_default(&cfg, HD_PROFILE_UL2034);
    (void)hd_node_init(&n, &cfg);
    run_seconds(&n, &cfg, 500, 0.0, 22.0, 0.0);
    HT_CHECK((n.faults & HD_FAULT_UNCALIBRATED) != 0u);
}

static void test_clean_air_calibration(void)
{
    hd_config_t cfg;
    hd_node_t n;
    int32_t c7 = 0;
    int32_t c135 = 0;
    bench_config(&cfg, HD_PROFILE_UL2034);
    (void)hd_node_init(&n, &cfg);
    HT_CHECK_EQ(hd_node_calibrate_clean_air(&n, &cfg, &c7, &c135), HD_ERR_RANGE);
    run_seconds(&n, &cfg, 460, 0.0, 22.0, 0.0);
    HT_CHECK_EQ(hd_node_calibrate_clean_air(&n, &cfg, &c7, &c135), HD_OK);
    HT_CHECK_NEAR(c7 / 65536.0, 0.0, 0.01);          /* bench uses RL == R0 */
    HT_CHECK_NEAR(c135 / 65536.0, 0.0, 0.01);
}

void test_node_all(void)
{
    ht_run("node.clean_air_quiet", test_clean_air_is_quiet);
    ht_run("node.co_400ppm_alarm_in_window", test_co_400ppm_alarm_in_window);
    ht_run("node.mq7_open_trouble", test_mq7_open_raises_trouble);
    ht_run("node.heater_fault", test_heater_fault);
    ht_run("node.flaming_fire_alarm", test_flaming_fire_alarm);
    ht_run("node.fast_path", test_fast_path_through_node);
    ht_run("node.reset_fault_ack", test_reset_fault_ack);
    ht_run("node.uncalibrated_trouble", test_uncalibrated_is_trouble);
    ht_run("node.clean_air_calibration", test_clean_air_calibration);
}
