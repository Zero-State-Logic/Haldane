/*
 * HALDANE - bare-metal CO + fire safety node
 * Copyright (c) 2026 Faysal Ali Shah - MIT License
 *
 * hd_cfk.c - fixed-point Coburn-Forster-Kane integrator (see hd_cfk.h).
 *
 * Coefficient algebra (integer units: dl = DL*10, hb = Hb*10 g/dL,
 * vco in uL/min, everything else as in hd_cfk_physio_t):
 *
 *   PL      = PB - 47
 *   B       = (10*va + PL*dl) / (dl*va)                 [min*mmHg/mL]
 *   Hbmax   = 138*hb / 100000                           [mL CO / mL blood]
 *   a*dt    = 5*vco / (414*vb*hb)                       [1/s]
 *   b*dt    = PL*dl*va / (9600*(10*va + PL*dl)*vb*138*hb)  per 1/16 ppm per s
 *   c*dt    = PO2*H*100000 / (8280*M*vb*hb),  H = dl*va/(10*va + PL*dl)
 *
 * The inspired CO pressure is PICO = ppm * 1e-6 * (PB - 47): CO mixed
 * into fully humidified inspired gas, consistent with the PL term in B.
 */
#include "hd_cfk.h"
#include "hd_fixed.h"

static const HD_ROM hd_cfk_physio_t k_presets[HD_OCC_COUNT] = {
    /* vb    va     dl   hb   M    PO2  vco  PB  */
    { 5500u,  6000u, 300u, 150u, 218u, 100u, 7u, 760u },  /* rest     */
    { 5500u, 10000u, 350u, 150u, 218u, 100u, 7u, 760u },  /* light    */
    { 5500u, 20000u, 400u, 150u, 218u, 100u, 7u, 760u },  /* moderate */
    { 5500u, 30000u, 450u, 150u, 218u, 100u, 7u, 760u },  /* heavy    */
};

int8_t hd_cfk_physio_preset(uint8_t id, hd_cfk_physio_t *out)
{
    if (out == 0) {
        return HD_ERR_NULL;
    }
    if (id >= (uint8_t)HD_OCC_COUNT) {
        return HD_ERR_RANGE;
    }
    *out = k_presets[id];
    return HD_OK;
}

static int8_t physio_in_range(const hd_cfk_physio_t *p)
{
    return (p->vb_ml >= 1000u && p->vb_ml <= 8000u &&
            p->va_ml_min >= 1000u && p->va_ml_min <= 60000u &&
            p->dl_x10 >= 50u && p->dl_x10 <= 1000u &&
            p->hb_g_dl_x10 >= 50u && p->hb_g_dl_x10 <= 250u &&
            p->haldane_m >= 150u && p->haldane_m <= 300u &&
            p->po2_mmhg >= 40u && p->po2_mmhg <= 150u &&
            p->vco_ul_min <= 100u &&
            p->pb_mmhg >= 500u && p->pb_mmhg <= 800u) ? 1 : 0;
}

/* Rounded unsigned 64-bit division (init-time only). */
static uint64_t udiv64_round(uint64_t num, uint64_t den)
{
    return (num + den / 2u) / den;
}

int8_t hd_cfk_derive(const hd_cfk_physio_t *p, hd_cfk_coeffs_t *k)
{
    uint64_t pl;
    uint64_t den_b;
    uint64_t g_q16;
    uint64_t h_q16;
    uint64_t blood;
    uint64_t v;

    if (p == 0 || k == 0) {
        return HD_ERR_NULL;
    }
    if (!physio_in_range(p)) {
        return HD_ERR_RANGE;
    }
    pl = (uint64_t)p->pb_mmhg - HD_CFK_PH2O_MMHG;
    den_b = UINT64_C(10) * p->va_ml_min + pl * p->dl_x10;
    blood = (uint64_t)p->vb_ml * p->hb_g_dl_x10;      /* vb*hb */

    /* uptake: b*dt per 1/16 ppm in Q46 = G * 2^46 / (9600*138*vb*hb) */
    g_q16 = udiv64_round((pl * p->dl_x10 * p->va_ml_min) << 16, den_b);
    v = udiv64_round(g_q16 << 30, UINT64_C(9600) * HD_CFK_HUFNER_X100 * blood);
    if (v == 0u || v > (uint64_t)INT32_MAX) {
        return HD_ERR_RANGE;
    }
    k->k_ppm_q46 = (int32_t)v;

    /* endogenous: a*dt in Q42 = 5*vco*2^42 / (414*vb*hb) */
    v = udiv64_round(((uint64_t)5u * p->vco_ul_min) << 42, UINT64_C(414) * blood);
    if (v > (uint64_t)INT32_MAX) {
        return HD_ERR_RANGE;
    }
    k->k_endo_q42 = (int32_t)v;

    /* elimination: c*dt in Q36 = PO2*H_q16*3125*2^22 / (1035*M*vb*hb) */
    h_q16 = udiv64_round(((uint64_t)p->dl_x10 * p->va_ml_min) << 16, den_b);
    v = udiv64_round(((uint64_t)p->po2_mmhg * h_q16 * UINT64_C(3125)) << 22,
                     UINT64_C(1035) * p->haldane_m * blood);
    if (v == 0u || v > (uint64_t)INT32_MAX) {
        return HD_ERR_RANGE;
    }
    k->k_elim_q36 = (int32_t)v;
    return HD_OK;
}

void hd_cfk_reset_baseline(hd_cfk_t *s, const hd_cfk_coeffs_t *k)
{
    /* Steady state at 0 ppm: x/(1-x) = a/c  ->  x = a/(a+c). */
    uint64_t a_q42 = (uint64_t)(uint32_t)k->k_endo_q42;
    uint64_t c_q42 = (uint64_t)(uint32_t)k->k_elim_q36 << 6;
    if (a_q42 + c_q42 == 0u) {
        s->x_q30 = 0;
        return;
    }
    s->x_q30 = (int32_t)udiv64_round(a_q42 << 30, a_q42 + c_q42);
}

void hd_cfk_set_pct_x100(hd_cfk_t *s, uint16_t pct_x100)
{
    /* x = pct/10000 in Q30, computed with the 32-bit divider. */
    uint32_t x = hd_udiv_frac(pct_x100, UINT32_C(10000), 30u);
    if (x > (uint32_t)HD_CFK_X_MAX_Q30) {
        x = (uint32_t)HD_CFK_X_MAX_Q30;
    }
    s->x_q30 = (int32_t)x;
}

void hd_cfk_step(hd_cfk_t *s, const hd_cfk_coeffs_t *k, uint16_t ppm_q4)
{
    int32_t x = s->x_q30;
    uint32_t one_minus_x;
    uint32_t r_q28;
    int64_t up_q46;
    int64_t endo_q46;
    int64_t elim_q46;
    int32_t dx_q30;

    one_minus_x = (uint32_t)HD_Q30_ONE - (uint32_t)x;      /* >= 0.1 in Q30 */
    r_q28 = hd_udiv_frac((uint32_t)x, one_minus_x, 28u);  /* x/(1-x), <= 9 */

    up_q46 = (int64_t)k->k_ppm_q46 * (int64_t)ppm_q4;
    endo_q46 = (int64_t)k->k_endo_q42 * 16;
    elim_q46 = hd_shr64_round((int64_t)k->k_elim_q36 * (int64_t)r_q28, 18u);

    dx_q30 = (int32_t)hd_shr64_round(up_q46 + endo_q46 - elim_q46, 16u);
    s->x_q30 = hd_clamp32(hd_sat_add32(x, dx_q30), 0, HD_CFK_X_MAX_Q30);
}

uint16_t hd_cfk_pct_x100(const hd_cfk_t *s)
{
    /* pct_x100 = x * 10000, x in Q30 */
    return (uint16_t)hd_mul_shr(s->x_q30, INT32_C(10000), 30u);
}

uint16_t hd_cfk_equilibrium_pct_x100(const hd_cfk_coeffs_t *k, uint16_t ppm_q4)
{
    /* x_ss = (a + b*ppm) / (a + b*ppm + c), all in Q46, normalised down to
     * 31 bits so the 32-bit divider can take it. */
    uint64_t num = (uint64_t)(uint32_t)k->k_ppm_q46 * ppm_q4 +
                   ((uint64_t)(uint32_t)k->k_endo_q42 << 4);
    uint64_t den = num + ((uint64_t)(uint32_t)k->k_elim_q36 << 10);
    uint32_t x_q30;
    while (den >= (UINT64_C(1) << 31)) {
        num >>= 1;
        den >>= 1;
    }
    if (den == 0u) {
        return 0u;
    }
    x_q30 = hd_udiv_frac((uint32_t)num, (uint32_t)den, 30u);
    return (uint16_t)hd_mul_shr((int32_t)x_q30, INT32_C(10000), 30u);
}
