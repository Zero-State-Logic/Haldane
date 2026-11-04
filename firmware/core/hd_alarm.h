/*
 * HALDANE - bare-metal CO + fire safety node
 * Copyright (c) 2026 Faysal Ali Shah - MIT License
 *
 * hd_alarm.h - alarm state machine (priorities, latching, hush) and the
 * millisecond annunciator (sounder patterns + LEDs).
 *
 * Priorities: FIRE > CO > TEST > hushed states > CO pre-alarm > TROUBLE
 *             > WARM-UP > NORMAL.
 *
 * Sounder patterns: fire uses the temporal-three pattern (3 x 0.5 s on,
 * 0.5 s off, then 1.5 s off); CO uses temporal-four (4 x 0.1 s on, 0.1 s
 * off, then 5 s off), the conventions used by residential smoke and CO
 * alarms. Trouble is a short chirp once a minute.
 *
 * Relay (exhaust fan): on for the CO pre-alarm, CO alarm and CO hush; OFF
 * whenever a fire alarm or fire hush is active (a fan would feed the fire
 * and spread smoke).
 *
 * Hush rules (design choices, not taken from a standard):
 *   fire: allowed only for FLAMING / SMOULDERING / SMOKE_HEAT causes,
 *         lasts fire_hush_s, re-sounds at once if a non-hushable cause
 *         (fixed heat, rate-of-rise, fast flame) appears.
 *   CO:   allowed only while estimated COHb is below co_no_hush_x100
 *         (default 10.00 %), lasts co_hush_s, re-sounds when COHb crosses
 *         co_no_hush_x100 or the hush expires with COHb still high.
 * Latching: each alarm sets a memory bit that survives the alarm and is
 * cleared by a short button press when nothing is active.
 */
#ifndef HD_ALARM_H
#define HD_ALARM_H

#include "hd_port.h"

enum { HD_BTN_NONE = 0, HD_BTN_SHORT = 1, HD_BTN_LONG = 2 };

enum {
    HD_SND_NONE = 0,
    HD_SND_FIRE_T3 = 1,
    HD_SND_CO_T4 = 2,
    HD_SND_CHIRP = 3,
    HD_SND_TEST = 4,
    HD_SND_COUNT = 5
};

enum {
    HD_STATE_NORMAL = 0,
    HD_STATE_WARMUP = 1,
    HD_STATE_TROUBLE = 2,
    HD_STATE_CO_WARN = 3,
    HD_STATE_CO_HUSH = 4,
    HD_STATE_FIRE_HUSH = 5,
    HD_STATE_TEST = 6,
    HD_STATE_CO_ALARM = 7,
    HD_STATE_FIRE_ALARM = 8
};

enum { HD_CO_IDLE = 0, HD_CO_WARN = 1, HD_CO_ALARM = 2, HD_CO_HUSH = 3 };
enum { HD_FS_IDLE = 0, HD_FS_ALARM = 1, HD_FS_HUSH = 2 };

#define HD_MEM_CO    0x01u
#define HD_MEM_FIRE  0x02u

/* Annunciator output bits. */
#define HD_OUT_BUZZER  0x01u
#define HD_OUT_RED     0x02u
#define HD_OUT_GREEN   0x04u

typedef struct {
    uint16_t co_alarm_x100;       /* COHb alarm level (x100 %)             */
    uint16_t co_warn_x100;        /* COHb pre-alarm level: fan on, silent  */
    uint16_t co_hyst_x100;        /* hysteresis below each level           */
    uint16_t co_no_hush_x100;     /* hush refused at/above this COHb       */
    uint16_t co_override_ppm_q4;  /* immediate alarm at this ppm (0 = off) */
    uint16_t co_hush_s;
    uint16_t fire_hush_s;
    uint16_t fire_min_on_s;       /* minimum fire alarm duration           */
    uint16_t fire_clear_s;        /* evidence-free seconds to clear        */
    uint8_t  test_s;
} hd_alarm_cfg_t;

typedef struct {
    uint16_t cohb_x100;
    uint16_t co_ppm_q4;
    uint16_t faults;              /* node fault word (non-zero = trouble)  */
    uint8_t  co_valid;            /* CO channel warm and healthy           */
    uint8_t  warming;             /* sensors still warming up              */
    uint8_t  fire_cause;          /* HD_FIRE_* from the detector           */
    uint8_t  button;              /* HD_BTN_*                              */
} hd_alarm_in_t;

typedef struct {
    uint16_t co_hush_left;
    uint16_t fire_hush_left;
    uint16_t fire_on_s;
    uint16_t fire_clear_run;
    uint8_t  co;                  /* HD_CO_*                               */
    uint8_t  fire;                /* HD_FS_*                               */
    uint8_t  fire_cause;          /* cause that raised the fire alarm      */
    uint8_t  memory;              /* HD_MEM_*                              */
    uint8_t  test_left;
    uint8_t  state;               /* HD_STATE_*                            */
    uint8_t  sounder;             /* HD_SND_*                              */
    uint8_t  relay;               /* 1 = exhaust fan on                    */
    uint8_t  ack;                 /* 1 for one step after an acknowledge   */
} hd_alarm_t;

typedef struct {
    uint16_t left_ms;
    uint16_t clock_ms;
    uint8_t  pattern;
    uint8_t  seg;
} hd_annun_t;

void hd_alarm_cfg_default(hd_alarm_cfg_t *cfg);
void hd_alarm_init(hd_alarm_t *a);

/* One-second update. */
void hd_alarm_step(hd_alarm_t *a, const hd_alarm_cfg_t *cfg, const hd_alarm_in_t *in);

/* Immediate fire alarm from the flame fast path (between seconds). */
void hd_alarm_force_fire(hd_alarm_t *a, uint8_t cause);

void hd_annun_init(hd_annun_t *n);

/* Millisecond tick: returns HD_OUT_* bits for this millisecond. */
uint8_t hd_annun_tick(hd_annun_t *n, uint8_t pattern, uint8_t state);

#endif /* HD_ALARM_H */
