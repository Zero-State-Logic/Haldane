/*
 * HALDANE - ATmega32 firmware
 * Copyright (c) 2026 Faysal Ali Shah - MIT License
 *
 * main.c - bare-metal main loop. All decisions are made by the portable
 * core (firmware/core/hd_node.c), the same code the host tests exercise.
 *
 *   ISRs (sampler.c)   1 kHz tick, ADC round-robin, INT0, UART TX
 *   main loop          100 Hz flame frames, INT0 edges, 1 s node step,
 *                      heater PWM, relay, CSV telemetry, LCD, watchdog
 *
 * The watchdog (2 s) is kicked only after a completed 1 s step, so a
 * stalled tick, ADC chain or main loop resets the MCU; the reset cause is
 * then latched as a trouble fault until acknowledged with the button.
 * Hold the button while powering up to run a clean-air calibration.
 */
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include "board.h"
#include "hal.h"

static hd_config_t g_cfg;
static hd_node_t g_node;
static uint8_t g_cal_mode;

static void jtag_off(void)
{
    uint8_t v = (uint8_t)(MCUCSR | (1u << JTD));
    MCUCSR = v;                 /* JTD must be written twice within 4 cycles */
    MCUCSR = v;
}

static void put_num(char *dst, uint32_t v, uint8_t width)
{
    uint8_t i = width;
    do {
        dst[--i] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v != 0u && i > 0u);
    while (i > 0u) {
        dst[--i] = ' ';
    }
}

/* Kept in flash (__flash) so they cost no SRAM. */
static const __flash char k_labels[9][11] = {
    "OK        ", "WARM-UP   ", "TROUBLE   ", "CO PRE-ALM", "CO HUSH   ",
    "FIRE HUSH ", "SELF TEST ", "CO ALARM! ", "FIRE!     "
};
static const __flash char k_cal_label[11] = "CAL CLEAN ";
static const __flash char k_header[] =
    "# HALDANE t_s,co_ppm,cohb_pct,cohb_eq_pct,temp_c,gas_idx,flame,state,cause,faults,heater\r\n";

static void uart_puts_flash(const __flash char *s)
{
    while (*s != '\0') {
        uart_putc(*s++);
    }
}

static void lcd_update(void)
{
    char l1[17] = "CO     Hb  .  % ";
    char l2[17] = "            .  C";
    const __flash char *lab = g_cal_mode ? k_cal_label
                              : k_labels[(g_node.alarm.state < 9u) ? g_node.alarm.state : 2u];
    uint8_t i;
    int32_t t = g_node.temp_cc < 0 ? 0 : g_node.temp_cc;
    put_num(&l1[2], (uint32_t)(g_node.co_ppm_q4 >> 4), 4u);
    put_num(&l1[9], (uint32_t)(g_node.cohb_x100 / 100u), 2u);
    put_num(&l1[12], (uint32_t)(g_node.cohb_x100 % 100u), 2u);
    if (l1[12] == ' ') {
        l1[12] = '0';
    }
    for (i = 0u; i < 10u; ++i) {
        l2[i] = lab[i];
    }
    put_num(&l2[10], (uint32_t)(t / 100), 2u);
    l2[13] = (char)('0' + (uint8_t)((t / 10) % 10));
    l2[14] = ' ';
    l1[16] = '\0';
    l2[16] = '\0';
    lcd_line(0u, l1);
    lcd_line(1u, l2);
}

static void telemetry(void)
{
    uart_put_fixed((int32_t)g_node.uptime_s, 0u);
    uart_putc(',');
    uart_put_fixed((int32_t)g_node.co_ppm_q4 * 10 / 16, 1u);
    uart_putc(',');
    uart_put_fixed(g_node.cohb_x100, 2u);
    uart_putc(',');
    uart_put_fixed(g_node.cohb_eq_x100, 2u);
    uart_putc(',');
    uart_put_fixed(g_node.temp_cc, 2u);
    uart_putc(',');
    uart_put_fixed(g_node.gas_q12 * 1000 / 4096, 3u);
    uart_putc(',');
    uart_put_fixed(hd_fire_flame_level(&g_node.fire), 0u);
    uart_putc(',');
    uart_put_fixed(g_node.alarm.state, 0u);
    uart_putc(',');
    uart_put_fixed(g_node.fire_cause, 0u);
    uart_putc(',');
    uart_put_fixed(g_node.faults, 0u);
    uart_putc(',');
    uart_put_fixed(hd_node_heater_duty(&g_node, &g_cfg), 0u);
    uart_puts("\r\n");
}

static void calibration_step(void)
{
    int32_t c7;
    int32_t c135;
    if (hd_node_calibrate_clean_air(&g_node, &g_cfg, &c7, &c135) != HD_OK) {
        return;                              /* still warming up */
    }
    cal_save(c7, c135);
    g_cfg.mq7_curve.log2_rl_r0_q16 = c7;
    g_cfg.mq135_curve.log2_rl_r0_q16 = c135;
    g_cfg.calibrated = 1u;
    g_cal_mode = 0u;
    uart_puts("# calibrated\r\n");
}

static void one_second(void)
{
    hd_node_in_t in;
    if (!sampler_take_second(&in)) {
        return;
    }
    in.button = sampler_take_button();
    hd_node_second(&g_node, &g_cfg, &in);
    io_heater(hd_node_heater_duty(&g_node, &g_cfg));
    io_relay(g_node.alarm.relay);
    if (g_cal_mode) {
        calibration_step();
    }
    telemetry();
    lcd_update();
    wdt_reset();
}

static void fatal_halt(void)
{
    lcd_line(0u, "CONFIG ERROR");
    for (;;) {
        PORTD |= (uint8_t)((1u << PIN_LED_RED) | (1u << PIN_BUZZER));
    }
}

int main(void)
{
    uint8_t reset_flags = MCUCSR;
    uint16_t sys_faults = 0u;
    uint16_t frame;
    uint8_t edges;

    MCUCSR = 0u;
    wdt_disable();
    jtag_off();
    io_init();
    uart_init();
    lcd_init();
    sampler_init();

    (void)hd_config_default(&g_cfg, HD_PROFILE_UL2034);
    g_cfg.calibrated = cal_load(&g_cfg.mq7_curve.log2_rl_r0_q16,
                                &g_cfg.mq135_curve.log2_rl_r0_q16);
    if (hd_node_init(&g_node, &g_cfg) != HD_OK) {
        fatal_halt();
    }
    if (reset_flags & (1u << WDRF)) {
        sys_faults |= HD_FAULT_RESET_WDT;
    }
    if (reset_flags & (1u << BORF)) {
        sys_faults |= HD_FAULT_RESET_BOD;
    }
    hd_node_note_reset(&g_node, sys_faults);
    g_cal_mode = sampler_button_down();

    uart_puts_flash(k_header);
    sei();
    wdt_enable(WDTO_2S);
    set_sleep_mode(SLEEP_MODE_IDLE);

    for (;;) {
        while (sampler_pop_flame(&frame)) {
            hd_node_flame_frame(&g_node, &g_cfg, frame);
        }
        for (edges = sampler_take_flame_edges(); edges > 0u; --edges) {
            hd_node_flame_edge(&g_node, &g_cfg);
        }
        sampler_set_annunciator(g_node.alarm.sounder, g_node.alarm.state);
        one_second();
        sleep_mode();            /* any interrupt (1 kHz tick at least) wakes us */
    }
}
