#ifndef TEST_RUNNER_H
#define TEST_RUNNER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ── Test framework macros ────────────────────────────────────────────────── */

/* Global counters - extern, defined in main.c */
extern int g_tests_run;
extern int g_tests_passed;
extern int g_tests_failed;

#define TEST_CASE(name) \
    static void test_##name##_impl(void); \
    static void __attribute__((constructor)) test_##name##_register(void) { \
        g_tests_run++; \
        fprintf(stderr, "[TEST] Running %s... ", #name); \
        test_##name##_impl(); \
    } \
    static void test_##name##_impl(void)

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAILED\n  Assertion failed: %s\n", #cond); \
            g_tests_failed++; \
            return; \
        } \
    } while(0)

#define ASSERT_FALSE(cond) \
    do { \
        if (cond) { \
            fprintf(stderr, "FAILED\n  Assertion failed: %s\n", #cond); \
            g_tests_failed++; \
            return; \
        } \
    } while(0)

#define ASSERT_EQ(expected, actual) \
    do { \
        if ((long)(expected) != (long)(actual)) { \
            fprintf(stderr, "FAILED\n  Expected: %ld, Actual: %ld\n", \
                    (long)(expected), (long)(actual)); \
            g_tests_failed++; \
            return; \
        } \
    } while(0)

#define ASSERT_STR_EQ(expected, actual) \
    do { \
        if (strcmp((expected), (actual)) != 0) { \
            fprintf(stderr, "FAILED\n  Expected: \"%s\", Actual: \"%s\"\n", \
                    (expected), (actual)); \
            g_tests_failed++; \
            return; \
        } \
    } while(0)

#define ASSERT_NOT_NULL(ptr) \
    do { \
        if ((ptr) == NULL) { \
            fprintf(stderr, "FAILED\n  Expected non-NULL pointer\n"); \
            g_tests_failed++; \
            return; \
        } \
    } while(0)

#define TEST_PASS() \
    do { \
        fprintf(stderr, "PASSED\n"); \
        g_tests_passed++; \
    } while(0)

#define TEST_REPORT() \
    do { \
        fprintf(stderr, "\n========================================\n"); \
        fprintf(stderr, "Tests run: %d\n", g_tests_run); \
        fprintf(stderr, "Passed: %d\n", g_tests_passed); \
        fprintf(stderr, "Failed: %d\n", g_tests_failed); \
        fprintf(stderr, "========================================\n"); \
        if (g_tests_failed > 0) { \
            fprintf(stderr, "RESULT: FAILURE\n"); \
            exit(1); \
        } else { \
            fprintf(stderr, "RESULT: SUCCESS\n"); \
            exit(0); \
        } \
    } while(0)

#endif /* TEST_RUNNER_H */
