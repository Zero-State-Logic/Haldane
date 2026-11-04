/*
 * HALDANE - bare-metal CO + fire safety node
 * Copyright (c) 2026 Faysal Ali Shah - MIT License
 *
 * hd_fire.c - fused multi-sensor fire detection (see hd_fire.h).
 *
 * Flicker band-pass: the difference of two first-order low-passes at
 * 100 Hz. alpha = 156/256 puts the upper corner at
 * -ln(1 - 0.609) * 100 / (2*pi) = 15.0 Hz, alpha = 30/256 the lower
 * corner at 2.0 Hz. The firmware averages 10 ADC samples (1 kHz) into
 * each 100 Hz frame, a boxcar with a null at 100 Hz, so mains-lamp
 * flicker at 100 Hz does not alias into the band.
 */
#include "hd_fire.h"
#include "hd_fixed.h"

#define PRIMED_TEMP   0x01u
#define PRIMED_GAS    0x02u
#define PRIMED_CO     0x04u
#define PRIMED_FLAME  0x08u

void hd_fire_cfg_default(hd_fire_cfg_t *cfg)
{
    cfg->lp_fast_a_q8 = 156u;
    cfg->lp_slow_a_q8 = 30u;
    cfg->energy_k = 6u;
    cfg->level_k = 7u;
    cfg->level_floor = 500u;
    cfg->energy_min = 40u;
    cfg->flame_level_min = 800u;
    cfg->flk_k_q8 = 13u;            /* 5 % relative flicker allowance   */
    cfg->flk_h = 160u;              /* ~ 6 s of 15 % excess flicker      */
    cfg->fast_level = 5000u;
    cfg->fast_rel_q8 = 26u;         /* 10 % relative flicker             */
    cfg->fast_confirm_ticks = 150u; /* 1.5 s                             */
    cfg->fast_window_ticks = 400u;  /* 4 s after an INT0 edge            */

    cfg->temp_smooth_k = 3u;
    cfg->ror_k = 4u;
    cfg->fixed_hold_s = 5u;
    cfg->ror_hold_s = 10u;
    cfg->temp_k_cc_min = 100;       /* 1.0 degC/min drift allowance      */
    cfg->temp_h_cc = 400;           /* 4.0 degC excess rise              */
    cfg->fixed_cc = 5700;           /* 57 degC fixed-temperature backup  */
    cfg->ror_cc_min = 833;          /* 8.3 degC/min (15 degF/min) backup */

    cfg->gas_smooth_k = 3u;
    cfg->gas_k_q12_min = 205;       /* 0.05 doublings per minute         */
    cfg->gas_h_q12 = 2458;          /* 0.6 doublings                     */

    cfg->co_k_q4_min = 32u;         /* 2 ppm/min allowance               */
    cfg->co_h_q4 = 400u;            /* 25 ppm excess rise                */
}

void hd_fire_init(hd_fire_t *f)
{
    f->lp_fast_q4 = 0;
    f->lp_slow_q4 = 0;
    f->energy_q8 = 0;
    f->level_q8 = 0;
    f->temp_s_q4 = 0;
    f->ror_q4 = 0;
    f->gas_s_q12 = 0;
    f->s_temp = 0;
    f->s_gas = 0;
    f->s_co = 0;
    f->s_flk = 0;
    f->co_prev_q4 = 0u;
    f->co_dt_s = 0u;
    f->fast_window = 0u;
    f->fast_count = 0u;
    f->fixed_run = 0u;
    f->ror_run = 0u;
    f->fast_fire = 0u;
    f->primed = 0u;
    f->cause = HD_FIRE_NONE;
    f->rel_q8 = 0u;
}

static int32_t cusum(int32_t s, int32_t inc, int32_t cap)
{
    return hd_clamp32(hd_sat_add32(s, inc), 0, cap);
}

static uint8_t inc_u8(uint8_t v)
{
    return (v < 255u) ? (uint8_t)(v + 1u) : v;
}

static void fast_path_frame(hd_fire_t *f, const hd_fire_cfg_t *cfg)
{
    /* The 2 Hz low-pass doubles as a fast level estimate (tau ~ 8 frames),
     * so a flame that has only just crossed the comparator threshold is
     * judged on its current brightness, not on the 1.3 s level average. */
    int32_t level_q8 = f->lp_slow_q4 * 16;
    int64_t lhs;
    int64_t rhs;
    --f->fast_window;
    /* strong = level >= fast_level AND energy/level >= fast_rel (no divide) */
    lhs = (int64_t)f->energy_q8 * 256;
    rhs = (int64_t)cfg->fast_rel_q8 * (int64_t)level_q8;
    if (f->lp_slow_q4 >= (int32_t)cfg->fast_level * 16 && lhs >= rhs) {
        if (f->fast_count < 0xFFFFu) {
            ++f->fast_count;
        }
        if (f->fast_count >= cfg->fast_confirm_ticks) {
            f->fast_fire = 1u;
        }
    } else {
        f->fast_count = 0u;
    }
}

void hd_fire_flame_frame(hd_fire_t *f, const hd_fire_cfg_t *cfg, uint16_t ir)
{
    int32_t x_q4 = (int32_t)ir * 16;
    int32_t band;
    if ((f->primed & PRIMED_FLAME) == 0u) {
        f->lp_fast_q4 = x_q4;
        f->lp_slow_q4 = x_q4;
        f->level_q8 = (int32_t)ir * 256;
        f->energy_q8 = 0;
        f->primed |= PRIMED_FLAME;
    }
    f->lp_fast_q4 = hd_ewma_q8(f->lp_fast_q4, x_q4, cfg->lp_fast_a_q8);
    f->lp_slow_q4 = hd_ewma_q8(f->lp_slow_q4, x_q4, cfg->lp_slow_a_q8);
    band = f->lp_fast_q4 - f->lp_slow_q4;
    if (band < 0) {
        band = -band;
    }
    f->energy_q8 = hd_ewma_shift(f->energy_q8, band * 16, cfg->energy_k);
    f->level_q8 = hd_ewma_shift(f->level_q8, (int32_t)ir * 256, cfg->level_k);
    if (f->fast_window > 0u) {
        fast_path_frame(f, cfg);
    }
}

void hd_fire_flame_edge(hd_fire_t *f, const hd_fire_cfg_t *cfg)
{
    if (f->fast_window == 0u) {
        f->fast_count = 0u;
    }
    f->fast_window = cfg->fast_window_ticks;
}

static void flame_second(hd_fire_t *f, const hd_fire_cfg_t *cfg, uint8_t valid)
{
    int32_t den = f->level_q8;
    int32_t floor_q8 = (int32_t)cfg->level_floor * 256;
    uint32_t rel;
    int32_t z = -(int32_t)cfg->flk_k_q8;
    if (den < floor_q8) {
        den = floor_q8;
    }
    rel = hd_udiv_frac((uint32_t)f->energy_q8, (uint32_t)den, 8u);
    f->rel_q8 = (rel > 255u) ? 255u : (uint8_t)rel;
    if (valid && (f->level_q8 >> 8) >= (int32_t)cfg->flame_level_min &&
        (f->energy_q8 >> 8) >= (int32_t)cfg->energy_min) {
        if (rel > UINT32_C(4096)) {
            rel = UINT32_C(4096);
        }
        z = (int32_t)rel - (int32_t)cfg->flk_k_q8;
    }
    f->s_flk = cusum(f->s_flk, z, INT32_C(4) * (int32_t)cfg->flk_h);
}

static void temp_second(hd_fire_t *f, const hd_fire_cfg_t *cfg, const hd_fire_in_t *in)
{
    int32_t t_q4 = in->temp_cc * 16;
    int32_t prev;
    int32_t d;
    int32_t h = (int32_t)cfg->temp_h_cc * INT32_C(960);
    if ((in->valid & HD_FIRE_V_TEMP) == 0u) {
        f->s_temp = 0;
        f->fixed_run = 0u;
        f->ror_run = 0u;
        f->primed &= (uint8_t)(0xFFu ^ PRIMED_TEMP);
        return;
    }
    if ((f->primed & PRIMED_TEMP) == 0u) {
        f->temp_s_q4 = t_q4;
        f->ror_q4 = 0;
        f->primed |= PRIMED_TEMP;
    }
    prev = f->temp_s_q4;
    f->temp_s_q4 = hd_ewma_shift(f->temp_s_q4, t_q4, cfg->temp_smooth_k);
    d = f->temp_s_q4 - prev;                       /* Q4 centi-degC per s */
    f->ror_q4 = hd_ewma_shift(f->ror_q4, d * 60, cfg->ror_k);
    f->s_temp = cusum(f->s_temp, d * 60 - (int32_t)cfg->temp_k_cc_min * 16,
                      INT32_C(4) * h);
    f->fixed_run = (in->temp_cc >= cfg->fixed_cc) ? inc_u8(f->fixed_run) : 0u;
    f->ror_run = (f->ror_q4 >= (int32_t)cfg->ror_cc_min * 16) ? inc_u8(f->ror_run) : 0u;
}

static void gas_second(hd_fire_t *f, const hd_fire_cfg_t *cfg, const hd_fire_in_t *in)
{
    int32_t prev;
    int32_t h = (int32_t)cfg->gas_h_q12 * 60;
    if ((in->valid & HD_FIRE_V_GAS) == 0u) {
        f->s_gas = 0;
        f->primed &= (uint8_t)(0xFFu ^ PRIMED_GAS);
        return;
    }
    if ((f->primed & PRIMED_GAS) == 0u) {
        f->gas_s_q12 = in->gas_q12;
        f->primed |= PRIMED_GAS;
    }
    prev = f->gas_s_q12;
    f->gas_s_q12 = hd_ewma_shift(f->gas_s_q12, in->gas_q12, cfg->gas_smooth_k);
    f->s_gas = cusum(f->s_gas, (f->gas_s_q12 - prev) * 60 - (int32_t)cfg->gas_k_q12_min,
                     INT32_C(4) * h);
}

static void co_second(hd_fire_t *f, const hd_fire_cfg_t *cfg, const hd_fire_in_t *in)
{
    int32_t delta;
    int32_t h = (int32_t)cfg->co_h_q4 * 60;
    if (f->co_dt_s < 0xFFFFu) {
        ++f->co_dt_s;
    }
    if ((in->valid & HD_FIRE_V_CO) == 0u) {
        f->s_co = 0;
        f->primed &= (uint8_t)(0xFFu ^ PRIMED_CO);
        return;
    }
    if (!in->co_new) {
        return;
    }
    if ((f->primed & PRIMED_CO) != 0u) {
        delta = (int32_t)in->co_ppm_q4 - (int32_t)f->co_prev_q4;
        f->s_co = cusum(f->s_co, delta * 60 - (int32_t)cfg->co_k_q4_min * (int32_t)f->co_dt_s,
                        INT32_C(4) * h);
    }
    f->co_prev_q4 = in->co_ppm_q4;
    f->co_dt_s = 0u;
    f->primed |= PRIMED_CO;
}

static uint8_t evaluate(const hd_fire_t *f, const hd_fire_cfg_t *cfg)
{
    int32_t h_t = (int32_t)cfg->temp_h_cc * INT32_C(960);
    int32_t h_g = (int32_t)cfg->gas_h_q12 * 60;
    int32_t h_c = (int32_t)cfg->co_h_q4 * 60;
    uint8_t half_any;

    if (f->fixed_run > 0u && f->fixed_run >= cfg->fixed_hold_s) {
        return HD_FIRE_HEAT_FIXED;
    }
    if (f->ror_run > 0u && f->ror_run >= cfg->ror_hold_s) {
        return HD_FIRE_HEAT_ROR;
    }
    if (f->fast_fire) {
        return HD_FIRE_FLAME_FAST;
    }
    half_any = (uint8_t)((f->s_temp * 2 >= h_t) || (f->s_gas * 2 >= h_g) ||
                         (f->s_co * 2 >= h_c));
    if (f->s_flk >= (int32_t)cfg->flk_h && half_any) {
        return HD_FIRE_FLAMING;
    }
    if (f->s_co >= h_c && f->s_gas >= h_g) {
        return HD_FIRE_SMOULDERING;
    }
    if (f->s_gas >= h_g && f->s_temp >= h_t) {
        return HD_FIRE_SMOKE_HEAT;
    }
    return HD_FIRE_NONE;
}

uint8_t hd_fire_second(hd_fire_t *f, const hd_fire_cfg_t *cfg, const hd_fire_in_t *in)
{
    flame_second(f, cfg, (uint8_t)((in->valid & HD_FIRE_V_FLAME) != 0u));
    temp_second(f, cfg, in);
    gas_second(f, cfg, in);
    co_second(f, cfg, in);
    f->cause = evaluate(f, cfg);
    f->fast_fire = 0u;
    return f->cause;
}

uint8_t hd_fire_hush_allowed(uint8_t cause)
{
    return (cause == HD_FIRE_FLAMING || cause == HD_FIRE_SMOULDERING ||
            cause == HD_FIRE_SMOKE_HEAT) ? 1u : 0u;
}

uint16_t hd_fire_flame_level(const hd_fire_t *f)
{
    return (uint16_t)(f->level_q8 >> 8);
}

uint16_t hd_fire_flame_energy(const hd_fire_t *f)
{
    return (uint16_t)(f->energy_q8 >> 8);
}
