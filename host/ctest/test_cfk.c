/*
 * HALDANE - host unit tests
 * Copyright (c) 2026 Faysal Ali Shah - MIT License
 *
 * test_cfk.c - fixed-point CFK against a double RK4 reference, UL 2034
 * windows (ideal sensing), long low-level exposure and short spikes.
 */
#include "ht.h"
#include "bench.h"

typedef double (*exposure_fn)(long t_s);

static double exp_const_ppm = 0.0;

static double exposure_const(long t_s)
{
    (void)t_s;
    return exp_const_ppm;
}

static double exposure_square(long t_s)
{
    /* 20 min at 180 ppm, 40 min at 10 ppm, repeating */
    return ((t_s % 3600L) < 1200L) ? 180.0 : 10.0;
}

static double exposure_ramp(long t_s)
{
    /* 0 -> 300 ppm over 4 h, then back down to 0 over 4 h */
    double h = (double)t_s / 3600.0;
    return (h < 4.0) ? 75.0 * h : 75.0 * (8.0 - h);
}

static uint16_t ppm_to_q4(double ppm)
{
    double v = ppm * 16.0 + 0.5;
    if (v < 0.0) {
        v = 0.0;
    }
    if (v > 65535.0) {
        v = 65535.0;
    }
    return (uint16_t)v;
}

/* Max |fixed - reference| in %COHb over n_s seconds. */
static double max_tracking_error(uint8_t preset, exposure_fn fn, long n_s)
{
    hd_cfk_physio_t p;
    hd_cfk_coeffs_t k;
    hd_cfk_t s;
    bench_cfk_ref_t r;
    double x;
    double worst = 0.0;
    long t;
    (void)hd_cfk_physio_preset(preset, &p);
    (void)hd_cfk_derive(&p, &k);
    hd_cfk_reset_baseline(&s, &k);
    bench_cfk_ref_init(&r, &p);
    x = bench_cfk_ref_baseline(&r);
    for (t = 0; t < n_s; ++t) {
        uint16_t q4 = ppm_to_q4(fn(t));
        double err;
        hd_cfk_step(&s, &k, q4);
        x = bench_cfk_ref_step(&r, x, q4 / 16.0, 1.0 / 60.0);
        err = fabs(s.x_q30 / 1073741824.0 - x) * 100.0;
        if (err > worst) {
            worst = err;
        }
    }
    return worst;
}

/* Minutes until %COHb x100 reaches thr at constant ppm (-1 if never). */
static double minutes_to(uint8_t preset, double ppm, uint16_t thr, long max_s)
{
    hd_cfk_physio_t p;
    hd_cfk_coeffs_t k;
    hd_cfk_t s;
    long t;
    (void)hd_cfk_physio_preset(preset, &p);
    (void)hd_cfk_derive(&p, &k);
    hd_cfk_reset_baseline(&s, &k);
    for (t = 1; t <= max_s; ++t) {
        hd_cfk_step(&s, &k, ppm_to_q4(ppm));
        if (hd_cfk_pct_x100(&s) >= thr) {
            return t / 60.0;
        }
    }
    return -1.0;
}

static void test_coefficients_match_double(void)
{
    uint8_t id;
    for (id = 0; id < (uint8_t)HD_OCC_COUNT; ++id) {
        hd_cfk_physio_t p;
        hd_cfk_coeffs_t k;
        bench_cfk_ref_t r;
        HT_CHECK_EQ(hd_cfk_physio_preset(id, &p), HD_OK);
        HT_CHECK_EQ(hd_cfk_derive(&p, &k), HD_OK);
        bench_cfk_ref_init(&r, &p);
        HT_CHECK_NEAR(k.k_ppm_q46 / 70368744177664.0 * 960.0 / r.b, 1.0, 1e-4);
        HT_CHECK_NEAR(k.k_endo_q42 / 4398046511104.0 * 60.0 / r.a, 1.0, 1e-4);
        HT_CHECK_NEAR(k.k_elim_q36 / 68719476736.0 * 60.0 / r.c, 1.0, 1e-4);
    }
}

static void test_derive_rejects_bad_params(void)
{
    hd_cfk_physio_t p;
    hd_cfk_coeffs_t k;
    (void)hd_cfk_physio_preset(HD_OCC_REST, &p);
    p.vb_ml = 100u;
    HT_CHECK_EQ(hd_cfk_derive(&p, &k), HD_ERR_RANGE);
    (void)hd_cfk_physio_preset(HD_OCC_REST, &p);
    p.po2_mmhg = 0u;
    HT_CHECK_EQ(hd_cfk_derive(&p, &k), HD_ERR_RANGE);
    HT_CHECK_EQ(hd_cfk_derive(0, &k), HD_ERR_NULL);
    HT_CHECK_EQ(hd_cfk_physio_preset(9u, &p), HD_ERR_RANGE);
}

static void test_baseline_and_equilibrium(void)
{
    hd_cfk_physio_t p;
    hd_cfk_coeffs_t k;
    hd_cfk_t s;
    bench_cfk_ref_t r;
    double ppm;
    (void)hd_cfk_physio_preset(HD_OCC_REST, &p);
    (void)hd_cfk_derive(&p, &k);
    bench_cfk_ref_init(&r, &p);
    hd_cfk_reset_baseline(&s, &k);
    HT_CHECK_NEAR(s.x_q30 / 1073741824.0, bench_cfk_ref_baseline(&r), 1e-6);
    for (ppm = 10.0; ppm <= 800.0; ppm *= 2.0) {
        double num = r.a + r.b * ppm;
        double ref = 100.0 * num / (num + r.c);
        HT_CHECK_NEAR(hd_cfk_equilibrium_pct_x100(&k, ppm_to_q4(ppm)) / 100.0, ref, 0.011);
    }
    hd_cfk_set_pct_x100(&s, 1234u);
    HT_CHECK_NEAR(hd_cfk_pct_x100(&s), 1234, 1);
}

static void test_tracks_reference_8h(void)
{
    uint8_t id;
    double worst = 0.0;
    for (id = 0; id < (uint8_t)HD_OCC_COUNT; ++id) {
        double e;
        exp_const_ppm = 70.0;
        e = max_tracking_error(id, exposure_const, 8L * 3600L);
        worst = (e > worst) ? e : worst;
        exp_const_ppm = 400.0;
        e = max_tracking_error(id, exposure_const, 8L * 3600L);
        worst = (e > worst) ? e : worst;
        e = max_tracking_error(id, exposure_square, 8L * 3600L);
        worst = (e > worst) ? e : worst;
        e = max_tracking_error(id, exposure_ramp, 8L * 3600L);
        worst = (e > worst) ? e : worst;
    }
    printf("    worst |fixed - float| over 8 h, 4 presets x 4 exposures: %.5f %%COHb\n", worst);
    HT_CHECK(worst < 0.1);
}

static void test_ul2034_windows_ideal_sensing(void)
{
    /* CFK only, CO known exactly and instantly: the default UL profile
     * (heavy exertion, 5.50 %) must alarm inside every UL 2034 window. */
    double t70 = minutes_to(HD_OCC_HEAVY, 70.0, 550u, 600L * 60L);
    double t150 = minutes_to(HD_OCC_HEAVY, 150.0, 550u, 120L * 60L);
    double t400 = minutes_to(HD_OCC_HEAVY, 400.0, 550u, 60L * 60L);
    printf("    ideal-sensing alarm times: 70 ppm %.1f min, 150 ppm %.1f min, 400 ppm %.1f min\n",
           t70, t150, t400);
    HT_CHECK(t70 >= 60.0 && t70 <= 240.0);
    HT_CHECK(t150 >= 10.0 && t150 <= 50.0);
    HT_CHECK(t400 >= 4.0 && t400 <= 15.0);
}

static void test_ten_percent_cannot_meet_70ppm_window(void)
{
    /* The finding behind the 5.5 % default: at 70 ppm no preset reaches
     * 10 % COHb within the 240 min UL limit. */
    uint8_t id;
    for (id = 0; id < (uint8_t)HD_OCC_COUNT; ++id) {
        HT_CHECK(minutes_to(id, 70.0, 1000u, 240L * 60L) < 0.0);
    }
}

static void test_no_alarm_30ppm_30_days(void)
{
    hd_cfk_physio_t p;
    hd_cfk_coeffs_t k;
    hd_cfk_t s;
    long t;
    uint16_t peak = 0u;
    (void)hd_cfk_physio_preset(HD_OCC_HEAVY, &p);
    (void)hd_cfk_derive(&p, &k);
    hd_cfk_reset_baseline(&s, &k);
    for (t = 0; t < 30L * 86400L; ++t) {
        uint16_t v;
        hd_cfk_step(&s, &k, ppm_to_q4(30.0));
        v = hd_cfk_pct_x100(&s);
        peak = (v > peak) ? v : peak;
    }
    printf("    30 ppm for 30 days: peak %.2f %%COHb (alarm at 5.50)\n", peak / 100.0);
    HT_CHECK(peak < 550u);
}

static void test_cooking_spike_no_alarm(void)
{
    hd_cfk_physio_t p;
    hd_cfk_coeffs_t k;
    hd_cfk_t s;
    long t;
    uint16_t peak = 0u;
    (void)hd_cfk_physio_preset(HD_OCC_HEAVY, &p);
    (void)hd_cfk_derive(&p, &k);
    hd_cfk_reset_baseline(&s, &k);
    for (t = 0; t < 3600L; ++t) {
        uint16_t v;
        hd_cfk_step(&s, &k, ppm_to_q4((t < 300L) ? 200.0 : 0.0));
        v = hd_cfk_pct_x100(&s);
        peak = (v > peak) ? v : peak;
    }
    HT_CHECK(peak < 300u);     /* below even the 3 % pre-alarm */
}

static void test_elimination_half_life_at_rest(void)
{
    hd_cfk_physio_t p;
    hd_cfk_coeffs_t k;
    hd_cfk_t s;
    long t;
    (void)hd_cfk_physio_preset(HD_OCC_REST, &p);
    (void)hd_cfk_derive(&p, &k);
    hd_cfk_set_pct_x100(&s, 2000u);
    for (t = 1; t < 12L * 3600L; ++t) {
        hd_cfk_step(&s, &k, 0u);
        if (hd_cfk_pct_x100(&s) <= 1000u) {
            break;
        }
    }
    printf("    resting half-life from 20 %%COHb in room air: %.0f min\n", t / 60.0);
    HT_CHECK(t / 60.0 > 180.0 && t / 60.0 < 420.0);
}

static void test_clamps(void)
{
    hd_cfk_physio_t p;
    hd_cfk_coeffs_t k;
    hd_cfk_t s;
    long t;
    (void)hd_cfk_physio_preset(HD_OCC_HEAVY, &p);
    (void)hd_cfk_derive(&p, &k);
    hd_cfk_reset_baseline(&s, &k);
    for (t = 0; t < 4L * 3600L; ++t) {
        hd_cfk_step(&s, &k, 65535u);    /* 4096 ppm */
    }
    HT_CHECK(s.x_q30 <= HD_CFK_X_MAX_Q30);
    HT_CHECK(hd_cfk_pct_x100(&s) <= 9000u);
    hd_cfk_set_pct_x100(&s, 0u);
    hd_cfk_step(&s, &k, 0u);
    HT_CHECK(s.x_q30 >= 0);
}

void test_cfk_all(void)
{
    ht_run("cfk.coefficients_match_double", test_coefficients_match_double);
    ht_run("cfk.derive_rejects_bad_params", test_derive_rejects_bad_params);
    ht_run("cfk.baseline_and_equilibrium", test_baseline_and_equilibrium);
    ht_run("cfk.tracks_float_reference_8h", test_tracks_reference_8h);
    ht_run("cfk.ul2034_windows_ideal_sensing", test_ul2034_windows_ideal_sensing);
    ht_run("cfk.ten_percent_cannot_meet_70ppm", test_ten_percent_cannot_meet_70ppm_window);
    ht_run("cfk.no_alarm_30ppm_30_days", test_no_alarm_30ppm_30_days);
    ht_run("cfk.cooking_spike_no_alarm", test_cooking_spike_no_alarm);
    ht_run("cfk.elimination_half_life_rest", test_elimination_half_life_at_rest);
    ht_run("cfk.clamps", test_clamps);
}
