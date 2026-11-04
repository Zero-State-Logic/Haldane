/*
 * HALDANE - ATmega32 firmware
 * Copyright (c) 2026 Faysal Ali Shah - MIT License
 *
 * hal.h - register-level drivers used by main.c.
 */
#ifndef HAL_H
#define HAL_H

#include <stdint.h>
#include "hd_node.h"

/* sampler.c: Timer1 1 kHz tick, ADC round-robin ISR, flame FIFO, INT0,
 * button debounce, annunciator outputs. */
void sampler_init(void);
uint8_t sampler_take_second(hd_node_in_t *in);  /* 1 when a second is ready */
uint8_t sampler_pop_flame(uint16_t *raw_sum10); /* 1 when a frame was popped */
uint8_t sampler_take_flame_edges(void);
uint8_t sampler_take_button(void);              /* HD_BTN_* */
uint8_t sampler_button_down(void);
void sampler_set_annunciator(uint8_t pattern, uint8_t state);

/* io.c: discrete outputs, MQ-7 heater PWM (Timer2) */
void io_init(void);
void io_relay(uint8_t on);
void io_heater(uint8_t duty);                   /* 255 = DC on, 0 = off */

/* uart.c: interrupt-driven TX ring buffer, never blocks */
void uart_init(void);
void uart_puts(const char *s);
void uart_put_fixed(int32_t v, uint8_t decimals);
void uart_putc(char c);

/* lcd.c: HD44780 16x2, 4-bit */
void lcd_init(void);
void lcd_line(uint8_t row, const char *text16);

/* cal_eeprom.c: clean-air calibration storage */
uint8_t cal_load(int32_t *mq7_log2_q16, int32_t *mq135_log2_q16);
void cal_save(int32_t mq7_log2_q16, int32_t mq135_log2_q16);

#endif /* HAL_H */
