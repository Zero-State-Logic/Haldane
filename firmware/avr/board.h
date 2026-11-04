/*
 * HALDANE - ATmega32 firmware
 * Copyright (c) 2026 Faysal Ali Shah - MIT License
 *
 * board.h - pin map (ATmega32, DIP-40) and clock.
 *
 * Fuses (16 MHz crystal, brown-out at 4.0 V, JTAG off, ISP on):
 *   lfuse 0x3F  BODLEVEL=0 (4.0 V) BODEN=0 (on) SUT=11 CKSEL=1111
 *   hfuse 0xC9  OCDEN=1 JTAGEN=1 (off) SPIEN=0 CKOPT=0 (needed for 16 MHz)
 *               EESAVE=1 BOOTSZ=00 BOOTRST=1
 */
#ifndef BOARD_H
#define BOARD_H

#ifndef F_CPU
#define F_CPU 16000000UL
#endif

#include <avr/io.h>

/* ADC channels (AVCC reference, MQ modules powered from the same 5 V) */
#define ADC_CH_MQ7     0u   /* PA0, pin 40: MQ-7 AO       */
#define ADC_CH_MQ135   1u   /* PA1, pin 39: MQ-135 AO     */
#define ADC_CH_LM35    2u   /* PA2, pin 38: LM35 Vout     */
#define ADC_CH_FLAME   3u   /* PA3, pin 37: flame AO      */

/* PORTD */
#define PIN_FLAME_DO   PD2  /* pin 16, INT0, flame DO (active low)        */
#define PIN_RELAY      PD3  /* pin 17, relay driver -> exhaust fan        */
#define PIN_BUZZER     PD4  /* pin 18, NPN -> active buzzer               */
#define PIN_LED_RED    PD5  /* pin 19                                     */
#define PIN_LED_GREEN  PD6  /* pin 20                                     */
#define PIN_HEATER     PD7  /* pin 21, OC2 PWM -> MQ-7 heater MOSFET gate */

/* PORTB */
#define PIN_BUTTON     PB2  /* pin 3, test/hush button to GND (pull-up)   */

/* PORTC: HD44780 in 4-bit mode, RW tied to GND (JTAG must be off) */
#define LCD_RS         PC0  /* pin 22 */
#define LCD_E          PC1  /* pin 23 */
                            /* D4..D7 = PC4..PC7, pins 26..29 */

#define UART_BAUD      38400UL   /* UBRR 25 at 16 MHz: 0.16 % error */

#endif /* BOARD_H */
