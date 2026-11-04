/*
 * HALDANE - ATmega32 firmware
 * Copyright (c) 2026 Faysal Ali Shah - MIT License
 *
 * sampler.c - the real-time front end.
 *
 *   Timer1 CTC, 1 kHz: starts an ADC round (4 channels), samples the flame
 *   comparator, debounces the button, drives buzzer and LEDs from the
 *   core annunciator.
 *   ADC complete ISR: accumulates sum/min/max per channel, chains to the
 *   next channel, builds 100 Hz flame frames (10 samples each) into a FIFO,
 *   and after 1000 rounds swaps the double buffer and flags a new second.
 *   INT0 ISR: counts flame-comparator edges (the fast path).
 * At 125 kHz ADC clock a conversion takes 104 us, so a round is ~416 us.
 */
#include <avr/interrupt.h>
#include "board.h"
#include "hal.h"

#define FLAME_FIFO_LEN 16u   /* power of two */

typedef struct {
    uint32_t sum[HD_CH_COUNT];
    uint16_t min[HD_CH_COUNT];
    uint16_t max[HD_CH_COUNT];
    uint16_t do_ms;
} acc_t;

static volatile acc_t g_acc[2];
static volatile uint8_t g_wr;            /* buffer the ISRs write into */
static volatile uint8_t g_second_ready;
static volatile uint8_t g_adc_ch;
static uint16_t s_rounds;

static volatile uint16_t g_fifo[FLAME_FIFO_LEN];
static volatile uint8_t g_fifo_head;
static volatile uint8_t g_fifo_tail;
static uint16_t s_flame_sum;
static uint8_t s_flame_n;

static volatile uint8_t g_flame_edges;
static volatile uint8_t g_button_event;
static uint8_t s_btn_integ;
static uint8_t s_btn_down;
static uint8_t s_btn_long_sent;
static uint16_t s_btn_held_ms;

static hd_annun_t s_annun;
static volatile uint8_t g_ann_pattern;
static volatile uint8_t g_ann_state;

static void acc_clear(volatile acc_t *a)
{
    uint8_t ch;
    for (ch = 0u; ch < (uint8_t)HD_CH_COUNT; ++ch) {
        a->sum[ch] = 0u;
        a->min[ch] = 0x3FFu;
        a->max[ch] = 0u;
    }
    a->do_ms = 0u;
}

void sampler_init(void)
{
    acc_clear(&g_acc[0]);
    acc_clear(&g_acc[1]);
    hd_annun_init(&s_annun);
    DDRB &= (uint8_t)~(1u << PIN_BUTTON);
    PORTB |= (uint8_t)(1u << PIN_BUTTON);              /* pull-up */
    DDRD &= (uint8_t)~(1u << PIN_FLAME_DO);
    PORTD |= (uint8_t)(1u << PIN_FLAME_DO);
    /* ADC: AVCC reference, /128 prescaler (125 kHz), interrupt on complete */
    ADMUX = (uint8_t)(1u << REFS0);
    ADCSRA = (uint8_t)((1u << ADEN) | (1u << ADIE) | (1u << ADPS2) | (1u << ADPS1) | (1u << ADPS0));
    /* Timer1 CTC: 16 MHz / 64 / 250 = 1 kHz */
    TCCR1A = 0u;
    TCCR1B = (uint8_t)((1u << WGM12) | (1u << CS11) | (1u << CS10));
    OCR1A = 249u;
    TIMSK |= (uint8_t)(1u << OCIE1A);
    /* INT0 on any edge of the flame comparator */
    MCUCR = (uint8_t)((MCUCR & 0xFCu) | (1u << ISC00));
    GICR |= (uint8_t)(1u << INT0);
}

static void button_tick(void)
{
    uint8_t pressed = (uint8_t)((PINB & (1u << PIN_BUTTON)) == 0u);
    if (pressed && s_btn_integ < 20u) {
        ++s_btn_integ;
    } else if (!pressed && s_btn_integ > 0u) {
        --s_btn_integ;
    }
    if (!s_btn_down && s_btn_integ == 20u) {
        s_btn_down = 1u;
        s_btn_held_ms = 0u;
        s_btn_long_sent = 0u;
    } else if (s_btn_down && s_btn_integ == 0u) {
        s_btn_down = 0u;
        if (!s_btn_long_sent && s_btn_held_ms < 2000u) {
            g_button_event = HD_BTN_SHORT;
        }
    }
    if (s_btn_down && s_btn_held_ms < 0xFFFFu) {
        ++s_btn_held_ms;
        if (s_btn_held_ms == 3000u) {
            g_button_event = HD_BTN_LONG;
            s_btn_long_sent = 1u;
        }
    }
}

ISR(TIMER1_COMPA_vect)
{
    uint8_t out;
    g_adc_ch = 0u;
    ADMUX = (uint8_t)((1u << REFS0) | ADC_CH_MQ7);
    ADCSRA |= (uint8_t)(1u << ADSC);
    if ((PIND & (1u << PIN_FLAME_DO)) == 0u) {
        ++g_acc[g_wr].do_ms;
    }
    button_tick();
    out = hd_annun_tick(&s_annun, g_ann_pattern, g_ann_state);
    PORTD = (uint8_t)((PORTD & (uint8_t)~((1u << PIN_BUZZER) | (1u << PIN_LED_RED) | (1u << PIN_LED_GREEN)))
                      | ((out & HD_OUT_BUZZER) ? (1u << PIN_BUZZER) : 0u)
                      | ((out & HD_OUT_RED) ? (1u << PIN_LED_RED) : 0u)
                      | ((out & HD_OUT_GREEN) ? (1u << PIN_LED_GREEN) : 0u));
}

static void flame_sample(uint16_t v)
{
    s_flame_sum = (uint16_t)(s_flame_sum + v);
    if (++s_flame_n >= 10u) {
        uint8_t next = (uint8_t)((g_fifo_head + 1u) & (FLAME_FIFO_LEN - 1u));
        if (next != g_fifo_tail) {               /* drop the frame if full */
            g_fifo[g_fifo_head] = s_flame_sum;
            g_fifo_head = next;
        }
        s_flame_sum = 0u;
        s_flame_n = 0u;
    }
}

ISR(ADC_vect)
{
    uint16_t v = ADC;
    uint8_t ch = g_adc_ch;
    volatile acc_t *a = &g_acc[g_wr];
    a->sum[ch] += v;
    if (v < a->min[ch]) {
        a->min[ch] = v;
    }
    if (v > a->max[ch]) {
        a->max[ch] = v;
    }
    if (ch == ADC_CH_FLAME) {
        flame_sample(v);
    }
    if (ch < ADC_CH_FLAME) {
        g_adc_ch = (uint8_t)(ch + 1u);
        ADMUX = (uint8_t)((1u << REFS0) | (ch + 1u));
        ADCSRA |= (uint8_t)(1u << ADSC);
        return;
    }
    if (++s_rounds >= 1000u) {
        s_rounds = 0u;
        g_wr ^= 1u;
        acc_clear(&g_acc[g_wr]);
        g_second_ready = 1u;
    }
}

ISR(INT0_vect)
{
    if (g_flame_edges < 255u) {
        ++g_flame_edges;
    }
}

uint8_t sampler_take_second(hd_node_in_t *in)
{
    volatile acc_t *a;
    uint8_t ch;
    if (!g_second_ready) {
        return 0u;
    }
    g_second_ready = 0u;
    a = &g_acc[g_wr ^ 1u];            /* stable for the next second */
    for (ch = 0u; ch < (uint8_t)HD_CH_COUNT; ++ch) {
        in->mean_q4[ch] = (uint16_t)((a->sum[ch] * 16u + 500u) / 1000u);
        in->raw_min[ch] = a->min[ch];
        in->raw_max[ch] = a->max[ch];
    }
    in->flame_do_ms = a->do_ms;
    in->button = HD_BTN_NONE;
    return 1u;
}

uint8_t sampler_pop_flame(uint16_t *raw_sum10)
{
    if (g_fifo_tail == g_fifo_head) {
        return 0u;
    }
    *raw_sum10 = g_fifo[g_fifo_tail];
    g_fifo_tail = (uint8_t)((g_fifo_tail + 1u) & (FLAME_FIFO_LEN - 1u));
    return 1u;
}

uint8_t sampler_take_flame_edges(void)
{
    uint8_t n;
    cli();
    n = g_flame_edges;
    g_flame_edges = 0u;
    sei();
    return n;
}

uint8_t sampler_take_button(void)
{
    uint8_t e;
    cli();
    e = g_button_event;
    g_button_event = HD_BTN_NONE;
    sei();
    return e;
}

uint8_t sampler_button_down(void)
{
    return (uint8_t)((PINB & (1u << PIN_BUTTON)) == 0u);
}

void sampler_set_annunciator(uint8_t pattern, uint8_t state)
{
    g_ann_pattern = pattern;     /* single-byte stores are atomic on AVR */
    g_ann_state = state;
}
