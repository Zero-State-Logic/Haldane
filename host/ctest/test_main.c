/*
 * HALDANE - host unit tests
 * Copyright (c) 2026 Faysal Ali Shah - MIT License
 *
 * test_main.c - runs every C unit test; exit code 0 only if all pass.
 */
#include "ht.h"

int ht_checks = 0;
int ht_failures = 0;
const char *ht_current = "";

static int ht_tests = 0;
static int ht_failed_tests = 0;

void ht_run(const char *name, ht_fn fn)
{
    int before = ht_failures;
    ht_current = name;
    fn();
    ++ht_tests;
    if (ht_failures != before) {
        ++ht_failed_tests;
        printf("[FAIL] %s\n", name);
    } else {
        printf("[ ok ] %s\n", name);
    }
}

void test_fixed_all(void);
void test_cfk_all(void);
void test_sensors_all(void);
void test_fire_all(void);
void test_alarm_all(void);
void test_node_all(void);

int main(void)
{
    test_fixed_all();
    test_cfk_all();
    test_sensors_all();
    test_fire_all();
    test_alarm_all();
    test_node_all();
    printf("\n%d tests, %d checks, %d failed tests, %d failed checks\n",
           ht_tests, ht_checks, ht_failed_tests, ht_failures);
    return (ht_failures == 0) ? 0 : 1;
}
