/*
 * HALDANE - bare-metal CO + fire safety node
 * Copyright (c) 2026 Faysal Ali Shah - MIT License
 *
 * hd_cfk.h - Coburn-Forster-Kane (CFK) carboxyhaemoglobin integrator in
 * 32-bit fixed point.
 *
 * The CFK equation (Coburn, Forster & Kane, J Clin Invest 1965) in the
 * non-linear form used by Peterson & Stewart (J Appl Physiol 1975):
 *
 *   d[COHb]/dt = VCO/Vb + PICO/(B*Vb) - [COHb]*PO2 / ([O2Hb]*M*B*Vb)
 *   B          = 1/DL + PL/VA,   PL = PB - 47 mmHg,   [O2Hb] = Hbmax - [COHb]
 *
 * We integrate the saturation x = [COHb]/Hbmax (0..1), so
 *
 *   dx/dt = a + b*ppm - c * x/(1-x)
 *
 * with a, b, c derived once from the physiological parameters. One step
 * is one second (forward Euler; every time constant here is > 30 min,
 * so the step error is far below the 0.1 %COHb accuracy target).
 *
 * Arithmetic: x in Q30, coefficients in Q36/Q42/Q46, 32x32->64 products,
 * a 32-bit restoring divider for x/(1-x). The per-second path contains
 * no 64-bit division and no floating point. hd_cfk_derive() and
 * hd_cfk_reset_baseline() run once at start-up and do use 64-bit
 * integer division.
 */
#ifndef HD_CFK_H
#define HD_CFK_H

#include "hd_port.h"

#define HD_CFK_PH2O_MMHG   47u     /* water vapour pressure at 37 degC     */
#define HD_CFK_HUFNER_X100 138u    /* 1.38 mL CO bound per g haemoglobin  */
#define HD_CFK_X_MAX_Q30   INT32_C(966367642)   /* clamp x at 0.90 (round(0.9 * 2^30)) */

/* Physiological parameters, integer engineering units. */
typedef struct {
    uint16_t vb_ml;        /* blood volume, mL                                */
    uint16_t va_ml_min;    /* alveolar ventilation, mL/min (STPD)             */
    uint16_t dl_x10;       /* lung diffusing capacity for CO x10, mL/min/mmHg */
    uint16_t hb_g_dl_x10;  /* haemoglobin x10, g/dL                           */
    uint16_t haldane_m;    /* Haldane constant M (CO vs O2 affinity ratio)    */
    uint16_t po2_mmhg;     /* mean pulmonary capillary O2 pressure, mmHg      */
    uint16_t vco_ul_min;   /* endogenous CO production, uL/min (STPD)         */
    uint16_t pb_mmhg;      /* barometric pressure, mmHg                       */
} hd_cfk_physio_t;

/* Per-second integration coefficients (see hd_cfk.c for the algebra). */
typedef struct {
    int32_t k_ppm_q46;     /* b*dt per 1/16 ppm, Q46 */
    int32_t k_endo_q42;    /* a*dt,              Q42 */
    int32_t k_elim_q36;    /* c*dt,              Q36 */
} hd_cfk_coeffs_t;

typedef struct {
    int32_t x_q30;         /* COHb saturation (fraction of Hbmax), Q30 */
} hd_cfk_t;

/* Occupant presets. All share the same standard adult (Vb 5500 mL,
 * Hb 15 g/dL, M 218, PO2 100 mmHg, VCO 7 uL/min, PB 760 mmHg) and
 * differ in activity: alveolar ventilation and diffusing capacity. */
enum {
    HD_OCC_REST = 0,       /* VA  6 L/min, DL 30 */
    HD_OCC_LIGHT = 1,      /* VA 10 L/min, DL 35 */
    HD_OCC_MODERATE = 2,   /* VA 20 L/min, DL 40 */
    HD_OCC_HEAVY = 3,      /* VA 30 L/min, DL 45 (worst-case uptake) */
    HD_OCC_COUNT = 4
};

/* Fill *out with a preset. Returns HD_OK or HD_ERR_RANGE. */
int8_t hd_cfk_physio_preset(uint8_t id, hd_cfk_physio_t *out);

/* Validate parameters and derive coefficients (init-time only). */
int8_t hd_cfk_derive(const hd_cfk_physio_t *p, hd_cfk_coeffs_t *k);

/* Set the state to the zero-ppm steady state (endogenous CO only). */
void hd_cfk_reset_baseline(hd_cfk_t *s, const hd_cfk_coeffs_t *k);

/* Set the state directly from a percentage (x100), e.g. 50 = 0.50 %. */
void hd_cfk_set_pct_x100(hd_cfk_t *s, uint16_t pct_x100);

/* Advance one second at a CO concentration of ppm_q4 / 16 ppm. */
void hd_cfk_step(hd_cfk_t *s, const hd_cfk_coeffs_t *k, uint16_t ppm_q4);

/* Current %COHb x100 (e.g. 550 = 5.50 %). */
uint16_t hd_cfk_pct_x100(const hd_cfk_t *s);

/* %COHb x100 the occupant would settle at if ppm_q4 persisted. */
uint16_t hd_cfk_equilibrium_pct_x100(const hd_cfk_coeffs_t *k, uint16_t ppm_q4);

#endif /* HD_CFK_H */
