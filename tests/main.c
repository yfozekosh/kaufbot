/* ── Main test runner ─────────────────────────────────────────────────────── */

#include "test_runner.h"
#include <stdio.h>

/* Global test counters */
int g_tests_run = 0;
int g_tests_passed = 0;
int g_tests_failed = 0;

int main(void)
{
    fprintf(stderr, "========================================\n");
    fprintf(stderr, "Kaufbot Test Suite\n");
    fprintf(stderr, "========================================\n\n");
    
    /* Tests run automatically via __attribute__((constructor)) */
    
    /* Print final report */
    TEST_REPORT();
    return 0;
}
