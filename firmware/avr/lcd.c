/*
 * HALDANE - ATmega32 firmware
 * Copyright (c) 2026 Faysal Ali Shah - MIT License
 *
 * lcd.c - HD44780 16x2 in 4-bit mode on PORTC (RS PC0, E PC1, D4..D7
 * PC4..PC7, RW tied low, so every command waits its datasheet time).
 * PC2..PC5 are JTAG pins: main.c turns JTAG off before calling lcd_init.
 */
#include <util/delay.h>
#include "board.h"
#include "hal.h"

static void lcd_nibble(uint8_t n)
{
    PORTC = (uint8_t)((PORTC & 0x0Fu) | (uint8_t)(n << 4));
    PORTC |= (uint8_t)(1u << LCD_E);
    _delay_us(1);
    PORTC &= (uint8_t)~(1u << LCD_E);
    _delay_us(1);
}

static void lcd_byte(uint8_t b, uint8_t is_data)
{
    if (is_data) {
        PORTC |= (uint8_t)(1u << LCD_RS);
    } else {
        PORTC &= (uint8_t)~(1u << LCD_RS);
    }
    lcd_nibble((uint8_t)(b >> 4));
    lcd_nibble((uint8_t)(b & 0x0Fu));
    _delay_us(50);
}

void lcd_init(void)
{
    DDRC |= (uint8_t)(0xF0u | (1u << LCD_RS) | (1u << LCD_E));
    _delay_ms(50);
    lcd_nibble(0x03u);          /* 8-bit, three times, then 4-bit */
    _delay_ms(5);
    lcd_nibble(0x03u);
    _delay_us(150);
    lcd_nibble(0x03u);
    _delay_us(150);
    lcd_nibble(0x02u);
    _delay_us(150);
    lcd_byte(0x28u, 0u);        /* 4-bit, 2 lines, 5x8 */
    lcd_byte(0x08u, 0u);        /* display off */
    lcd_byte(0x01u, 0u);        /* clear */
    _delay_ms(2);
    lcd_byte(0x06u, 0u);        /* entry: increment */
    lcd_byte(0x0Cu, 0u);        /* display on, no cursor */
}

void lcd_line(uint8_t row, const char *text16)
{
    uint8_t i;
    uint8_t ended = 0u;
    lcd_byte(row ? 0xC0u : 0x80u, 0u);
    for (i = 0u; i < 16u; ++i) {
        if (!ended && text16[i] == '\0') {
            ended = 1u;                      /* pad the rest with spaces */
        }
        lcd_byte((uint8_t)(ended ? ' ' : text16[i]), 1u);
    }
}
