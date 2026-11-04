/*
 * HALDANE - ATmega32 firmware
 * Copyright (c) 2026 Faysal Ali Shah - MIT License
 *
 * uart.c - 38400 8N1 telemetry. Characters go into a 128-byte ring and the
 * data-register-empty interrupt drains it. A full ring drops characters
 * rather than blocking the safety loop.
 */
#include <avr/interrupt.h>
#include "board.h"
#include "hal.h"

#define TX_LEN 128u   /* power of two */

static volatile char g_tx[TX_LEN];
static volatile uint8_t g_head;
static volatile uint8_t g_tail;

void uart_init(void)
{
    uint16_t ubrr = (uint16_t)(F_CPU / (16UL * UART_BAUD) - 1UL);   /* 25 */
    UBRRH = (uint8_t)(ubrr >> 8);
    UBRRL = (uint8_t)ubrr;
    UCSRA = 0u;
    UCSRB = (uint8_t)(1u << TXEN);
    UCSRC = (uint8_t)((1u << URSEL) | (1u << UCSZ1) | (1u << UCSZ0));
}

void uart_putc(char c)
{
    uint8_t next = (uint8_t)((g_head + 1u) & (TX_LEN - 1u));
    if (next == g_tail) {
        return;                      /* full: drop */
    }
    g_tx[g_head] = c;
    g_head = next;
    UCSRB |= (uint8_t)(1u << UDRIE);
}

void uart_puts(const char *s)
{
    while (*s != '\0') {
        uart_putc(*s++);
    }
}

void uart_put_fixed(int32_t v, uint8_t decimals)
{
    char buf[12];
    uint8_t n = 0u;
    uint32_t mag;
    if (v < 0) {
        uart_putc('-');
        mag = (uint32_t)(-(v + 1)) + 1u;
    } else {
        mag = (uint32_t)v;
    }
    do {
        buf[n++] = (char)('0' + (mag % 10u));
        mag /= 10u;
        if (n == decimals) {
            buf[n++] = '.';
        }
    } while (mag != 0u || n <= (uint8_t)(decimals + (decimals ? 1u : 0u)));
    while (n > 0u) {
        uart_putc(buf[--n]);
    }
}

ISR(USART_UDRE_vect)
{
    if (g_head == g_tail) {
        UCSRB &= (uint8_t)~(1u << UDRIE);
        return;
    }
    UDR = (uint8_t)g_tx[g_tail];
    g_tail = (uint8_t)((g_tail + 1u) & (TX_LEN - 1u));
}
