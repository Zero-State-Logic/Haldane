/*
 * HALDANE - host unit tests
 * Copyright (c) 2026 Faysal Ali Shah - MIT License
 *
 * ht.h - a tiny assert framework: counts checks, prints failures, and the
 * test runner exits non-zero if anything failed. Host-only (uses stdio).
 */
#ifndef HT_H
#define HT_H

#include <stdio.h>
#include <math.h>

extern int ht_checks;
extern int ht_failures;
extern const char *ht_current;

#define HT_CHECK(cond)                                                      \
    do {                                                                    \
        ++ht_checks;                                                        \
        if (!(cond)) {                                                      \
            ++ht_failures;                                                  \
            printf("  FAIL [%s] %s:%d: %s\n", ht_current, __FILE__, __LINE__, #cond); \
        }                                                                   \
    } while (0)

#define HT_CHECK_NEAR(a, b, tol)                                            \
    do {                                                                    \
        double ht_a_ = (double)(a);                                         \
        double ht_b_ = (double)(b);                                         \
        ++ht_checks;                                                        \
        if (!(fabs(ht_a_ - ht_b_) <= (double)(tol))) {                      \
            ++ht_failures;                                                  \
            printf("  FAIL [%s] %s:%d: %s=%g vs %s=%g (tol %g)\n", ht_current,  \
                   __FILE__, __LINE__, #a, ht_a_, #b, ht_b_, (double)(tol)); \
        }                                                                   \
    } while (0)

#define HT_CHECK_EQ(a, b)                                                   \
    do {                                                                    \
        long long ht_a_ = (long long)(a);                                   \
        long long ht_b_ = (long long)(b);                                   \
        ++ht_checks;                                                        \
        if (ht_a_ != ht_b_) {                                               \
            ++ht_failures;                                                  \
            printf("  FAIL [%s] %s:%d: %s=%lld != %s=%lld\n", ht_current,   \
                   __FILE__, __LINE__, #a, ht_a_, #b, ht_b_);               \
        }                                                                   \
    } while (0)

typedef void (*ht_fn)(void);

void ht_run(const char *name, ht_fn fn);

#endif /* HT_H */
