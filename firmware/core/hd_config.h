/*
 * HALDANE - bare-metal CO + fire safety node
 * Copyright (c) 2026 Faysal Ali Shah - MIT License
 *
 * hd_config.h - complete node configuration and the two shipped profiles.
 *
 * Profiles are configuration choices, not medical advice:
 *
 *   HD_PROFILE_UL2034     worst-case-uptake adult (heavy exertion), alarm
 *                         at 5.5 % estimated COHb. Chosen so the alarm
 *                         lands inside all three UL 2034 windows (see
 *                         docs/CFK.md for why 10 % cannot).
 *   HD_PROFILE_SENSITIVE  same occupant model, alarm at 3.3 % COHb plus an
 *                         immediate alarm at 250 ppm. Tracks the lower
 *                         EN 50291 alarm points, which are meant to protect
 *                         more vulnerable occupants; it deliberately gives
 *                         up the UL 2034 lower (nuisance) bounds.
 */
#ifndef HD_CONFIG_H
#define HD_CONFIG_H

#include "hd_port.h"
#include "hd_cfk.h"
#include "hd_calib.h"
#include "hd_mq7.h"
#include "hd_health.h"
#include "hd_fire.h"
#include "hd_alarm.h"

enum { HD_CH_MQ7 = 0, HD_CH_MQ135 = 1, HD_CH_LM35 = 2, HD_CH_FLAME = 3, HD_CH_COUNT = 4 };

enum { HD_PROFILE_UL2034 = 0, HD_PROFILE_SENSITIVE = 1, HD_PROFILE_COUNT = 2 };

#define HD_FLAME_FRAME_MAX 10230u   /* 10 ADC samples per 100 Hz frame */

typedef struct {
    hd_cfk_physio_t occupant;
    hd_alarm_cfg_t  alarm;
    hd_fire_cfg_t   fire;
    hd_mq7_cfg_t    mq7;
    hd_health_cfg_t health[HD_CH_COUNT];
    hd_mq_curve_t   mq7_curve;
    hd_mq_curve_t   mq135_curve;
    uint16_t vref_mv;          /* ADC reference (AVCC), mV                 */
    uint16_t heater_min_q4;    /* MQ-7 high-phase mean below this = fault  */
    uint8_t  flame_inverted;   /* 1: module output falls as IR rises       */
    uint8_t  calibrated;       /* 0 raises HD_FAULT_UNCALIBRATED           */
    uint8_t  profile;          /* HD_PROFILE_* this config was built from  */
} hd_config_t;

/* Fill a complete configuration for a profile. */
int8_t hd_config_default(hd_config_t *cfg, uint8_t profile);

#endif /* HD_CONFIG_H */
