/*
 * HALDANE - ATmega32 firmware
 * Copyright (c) 2026 Faysal Ali Shah - MIT License
 *
 * io.c - discrete outputs and the MQ-7 heater PWM.
 *
 * Timer2 fast PWM, /8 prescaler: 16 MHz / 8 / 256 = 7.8 kHz on OC2 (PD7),
 * far above the heater's thermal time constant. Non-inverting fast PWM
 * keeps the pin high for OCR2+1 counts, so OCR2 = duty-1 gives duty/256.
 * The 5 V phase disconnects OC2 and drives the pin high (true DC).
 */
#include "board.h"
#include "hal.h"

void io_init(void)
{
    DDRD |= (uint8_t)((1u << PIN_RELAY) | (1u << PIN_BUZZER) | (1u << PIN_LED_RED) |
                      (1u << PIN_LED_GREEN) | (1u << PIN_HEATER));
    PORTD &= (uint8_t)~((1u << PIN_RELAY) | (1u << PIN_BUZZER) | (1u << PIN_LED_RED) |
                        (1u << PIN_LED_GREEN));
    TCCR2 = (uint8_t)((1u << WGM21) | (1u << WGM20) | (1u << CS21));
    io_heater(255u);
}

void io_relay(uint8_t on)
{
    if (on) {
        PORTD |= (uint8_t)(1u << PIN_RELAY);
    } else {
        PORTD &= (uint8_t)~(1u << PIN_RELAY);
    }
}

void io_heater(uint8_t duty)
{
    if (duty == 255u || duty == 0u) {
        TCCR2 &= (uint8_t)~((1u << COM21) | (1u << COM20));   /* plain GPIO */
        if (duty) {
            PORTD |= (uint8_t)(1u << PIN_HEATER);
        } else {
            PORTD &= (uint8_t)~(1u << PIN_HEATER);
        }
        return;
    }
    OCR2 = (uint8_t)(duty - 1u);
    TCCR2 |= (uint8_t)(1u << COM21);
}
