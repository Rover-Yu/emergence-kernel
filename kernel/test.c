/* Emergence Kernel - Test Framework Implementation */

#include <stdint.h>
#include <stddef.h>
#include "kernel/test.h"
#include "arch/x86_64/multiboot2.h"
#include "arch/x86_64/serial.h"
#include "arch/x86_64/power.h"

/* External cmdline_get_value from multiboot2.c */
extern const char *cmdline_get_value(const char *key);

/* Test selection state */
static enum {
    TEST_MODE_NONE,      /* No test= parameter (default) */
    TEST_MODE_ALL,       /* test=all: Run all auto_run tests */
    TEST_MODE_SPECIFIC,  /* test=<name>: Run specific test */
    TEST_MODE_UNIFIED    /* test=unified: Run all selected at end */
} test_mode = TEST_MODE_NONE;

/* Specific test name (for TEST_MODE_SPECIFIC) */
static const char *selected_test_name = NULL;

/* Track which tests have already run (for unified mode) */
#define MAX_TESTS 16
static char tests_run[MAX_TESTS];  /* 0 = not run, 1 = run */
static int tests_run_count = 0;

/* ============================================================================
 * Simple String Functions (kernel has no string.h)
 * ============================================================================ */

/**
 * simple_strcmp - Compare two strings
 * @s1: First string
 * @s2: Second string
 *
 * Returns: 0 if equal, negative if s1 < s2, positive if s1 > s2
 */
static int simple_strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

/**
 * simple_strlen - Get string length
 * @s: String
 *
 * Returns: Length of string
 */
static size_t simple_strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

/* ============================================================================
 * Test Registry
 * ============================================================================ */

/* External test functions */
extern int run_pmm_tests(void);
extern int run_slab_tests(void);
extern int run_spinlock_tests(void);
extern int run_nk_protection_tests(void);

/* Test registry array */
const test_case_t test_registry[] = {
#if CONFIG_PMM_TESTS
    {
        .name = "pmm",
        .description = "Physical memory manager allocation tests",
        .run_func = run_pmm_tests,
        .enabled = 1,
        .auto_run = 1  /* Auto-run after pmm_init() */
    },
#endif
#if CONFIG_SLAB_TESTS
    {
        .name = "slab",
        .description = "Slab allocator small object allocation tests",
        .run_func = run_slab_tests,
        .enabled = 1,
        .auto_run = 1  /* Auto-run after slab_init() */
    },
#endif
#if CONFIG_SPINLOCK_TESTS
    {
        .name = "spinlock",
        .description = "Spin lock and read-write lock synchronization tests",
        .run_func = run_spinlock_tests,
        .enabled = 1,
        .auto_run = 1  /* Auto-run after AP startup */
    },
#endif
#if CONFIG_NK_PROTECTION_TESTS
    {
        .name = "nk_protection",
        .description = "Nested kernel mappings protection tests (destructive)",
        .run_func = run_nk_protection_tests,
        .enabled = 1,
        .auto_run = 0  /* Manual only (destructive test) */
    },
#endif
    { .name = NULL }  /* Sentinel */
};

/* ============================================================================
 * Framework Implementation
 * ============================================================================ */

/**
 * test_framework_init() - Initialize test framework from cmdline
 */
void test_framework_init(void) {
    const char *test_value;

    serial_puts("[TEST] Initializing test framework...\n");

    /* Initialize tests_run tracking array */
    for (int i = 0; i < MAX_TESTS; i++) {
        tests_run[i] = 0;
    }
    tests_run_count = 0;

    /* Parse test= parameter from cmdline */
    test_value = cmdline_get_value("test");

    if (test_value == NULL) {
        test_mode = TEST_MODE_NONE;
        serial_puts("[TEST] No test= parameter (default: no tests run)\n");
        return;
    }

    serial_puts("[TEST] test=");
    serial_puts(test_value);
    serial_puts("\n");

    /* Determine test mode */
    if (simple_strcmp(test_value, "all") == 0) {
        test_mode = TEST_MODE_ALL;
        serial_puts("[TEST] Mode: ALL (run all auto_run tests)\n");
    } else if (simple_strcmp(test_value, "unified") == 0) {
        test_mode = TEST_MODE_UNIFIED;
        serial_puts("[TEST] Mode: UNIFIED (run all selected tests at end)\n");
    } else {
        test_mode = TEST_MODE_SPECIFIC;
        selected_test_name = test_value;
        serial_puts("[TEST] Mode: SPECIFIC (test=");
        serial_puts(selected_test_name);
        serial_puts(")\n");
    }
}

/**
 * test_should_run() - Check if test should run in auto mode
 * @name: Test name
 *
 * Returns: 1 if test should run, 0 otherwise
 */
int test_should_run(const char *name) {
    /* No test= parameter: no tests run */
    if (test_mode == TEST_MODE_NONE) {
        return 0;
    }

    /* Unified mode: tests run at end, not during init */
    if (test_mode == TEST_MODE_UNIFIED) {
        return 0;  /* Will be run by test_run_unified() */
    }

    /* test=all: run all enabled tests with auto_run=1 */
    if (test_mode == TEST_MODE_ALL) {
        /* Find test in registry and check auto_run flag */
        for (int i = 0; test_registry[i].name != NULL; i++) {
            if (simple_strcmp(test_registry[i].name, name) == 0) {
                return test_registry[i].enabled && test_registry[i].auto_run;
            }
        }
        return 0;  /* Test not found */
    }

    /* test=<name>: run only the specified test */
    if (test_mode == TEST_MODE_SPECIFIC) {
        return (simple_strcmp(selected_test_name, name) == 0);
    }

    return 0;
}

/**
 * test_run_by_name() - Execute test by name with fail-fast
 * @name: Test name
 *
 * Returns: 0 on success, non-zero on failure (but shutdown occurs first)
 */
int test_run_by_name(const char *name) {
    const test_case_t *test = NULL;

    serial_puts("[TEST] Running test: ");
    serial_puts(name);
    serial_puts("\n");

    /* Find test in registry */
    for (int i = 0; test_registry[i].name != NULL; i++) {
        if (simple_strcmp(test_registry[i].name, name) == 0) {
            test = &test_registry[i];
            break;
        }
    }

    if (test == NULL) {
        serial_puts("[TEST] ERROR: Test '");
        serial_puts(name);
        serial_puts("' not found in registry\n");
        return -1;
    }

    if (!test->enabled) {
        serial_puts("[TEST] ERROR: Test '");
        serial_puts(name);
        serial_puts("' is not enabled (check CONFIG_ flag)\n");
        return -1;
    }

    /* Mark test as run */
    if (tests_run_count < MAX_TESTS) {
        tests_run[tests_run_count++] = 1;
    }

    /* Run test */
    serial_puts("[TEST] Description: ");
    serial_puts(test->description);
    serial_puts("\n");

    int result = test->run_func();

    if (result == 0) {
        serial_puts("[TEST] PASSED: ");
        serial_puts(name);
        serial_puts("\n");
    } else {
        serial_puts("[TEST] FAILED: ");
        serial_puts(name);
        serial_puts(" (failures: ");
        serial_put_hex(result);
        serial_puts(")\n");
        serial_puts("[TEST] FAILURE - System shutting down\n");
        system_shutdown();
        /* Never returns */
    }

    return result;
}

/**
 * test_run_unified() - Run all selected tests at unified point
 *
 * Returns: 0 on success, non-zero if any test failed
 */
int test_run_unified(void) {
    int failures = 0;

    /* Only run if in unified mode */
    if (test_mode != TEST_MODE_UNIFIED) {
        return 0;
    }

    serial_puts("\n");
    serial_puts("========================================\n");
    serial_puts("UNIFIED TEST EXECUTION\n");
    serial_puts("========================================\n");
    serial_puts("\n");

    /* Run all enabled tests that haven't run yet */
    for (int i = 0; test_registry[i].name != NULL; i++) {
        const test_case_t *test = &test_registry[i];

        if (!test->enabled) {
            continue;  /* Skip disabled tests */
        }

        /* Check if already run (shouldn't happen in unified mode, but be safe) */
        int already_run = 0;
        for (int j = 0; j < tests_run_count; j++) {
            if (tests_run[j]) {
                already_run = 1;
                break;
            }
        }
        if (already_run) {
            continue;
        }

        /* Run the test */
        serial_puts("[TEST] [");
        serial_put_hex(i + 1);
        serial_puts("/");
        serial_put_hex(i + 1);  /* Will update after counting total */
        serial_puts("] Running: ");
        serial_puts(test->name);
        serial_puts("\n");

        int result = test->run_func();

        if (result != 0) {
            serial_puts("[TEST] FAILED: ");
            serial_puts(test->name);
            serial_puts("\n");
            failures++;
            /* Continue running other tests to see all failures */
        } else {
            serial_puts("[TEST] PASSED: ");
            serial_puts(test->name);
            serial_puts("\n");
        }

        serial_puts("\n");
    }

    /* Summary */
    serial_puts("========================================\n");
    if (failures == 0) {
        serial_puts("UNIFIED: ALL TESTS PASSED\n");
    } else {
        serial_puts("UNIFIED: SOME TESTS FAILED\n");
        serial_puts("Failures: ");
        serial_put_hex(failures);
        serial_puts("\n");
    }
    serial_puts("========================================\n");
    serial_puts("\n");

    return failures;
}
