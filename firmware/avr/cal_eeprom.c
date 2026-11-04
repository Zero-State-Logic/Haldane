/*
 * HALDANE - ATmega32 firmware
 * Copyright (c) 2026 Faysal Ali Shah - MIT License
 *
 * cal_eeprom.c - clean-air calibration (log2 RL/R0 per MQ sensor) in EEPROM
 * with a magic word and a simple check word.
 */
#include <avr/eeprom.h>
#include "hal.h"

#define CAL_MAGIC 0x4844u   /* "HD" */

typedef struct {
    uint16_t magic;
    int32_t  mq7;
    int32_t  mq135;
    uint16_t check;
} cal_blob_t;

static cal_blob_t EEMEM ee_cal;

static uint16_t check_word(const cal_blob_t *b)
{
    return (uint16_t)(b->magic ^ (uint16_t)b->mq7 ^ (uint16_t)((uint32_t)b->mq7 >> 16) ^
                      (uint16_t)b->mq135 ^ (uint16_t)((uint32_t)b->mq135 >> 16) ^ 0xA5A5u);
}

uint8_t cal_load(int32_t *mq7_log2_q16, int32_t *mq135_log2_q16)
{
    cal_blob_t b;
    eeprom_read_block(&b, &ee_cal, sizeof(b));
    if (b.magic != CAL_MAGIC || b.check != check_word(&b)) {
        return 0u;
    }
    *mq7_log2_q16 = b.mq7;
    *mq135_log2_q16 = b.mq135;
    return 1u;
}

void cal_save(int32_t mq7_log2_q16, int32_t mq135_log2_q16)
{
    cal_blob_t b;
    b.magic = CAL_MAGIC;
    b.mq7 = mq7_log2_q16;
    b.mq135 = mq135_log2_q16;
    b.check = check_word(&b);
    eeprom_update_block(&b, &ee_cal, sizeof(b));
}
