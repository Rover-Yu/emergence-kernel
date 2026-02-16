/* Emergence Kernel - Test Framework Implementation */

#include <stdint.h>
#include <stddef.h>
#include "kernel/test.h"
#include "arch/x86_64/multiboot2.h"
#include "arch/x86_64/serial.h"
#include "arch/x86_64/power.h"
#include "kernel/klog.h"

/* External cmdline_get_value from multiboot2.c */
extern const char *cmdline_get_value(const char *key);

/* Test selection state */
static enum {
    TEST_MODE_NONE,      /* No tests= parameter (default) */
    TEST_MODE_ALL,       /* tests=all: Run all auto_run tests */
    TEST_MODE_UNIFIED    /* tests=unified: Run all selected at end */
    /* Note: TEST_MODE_SPECIFIC removed - tests=NAME1,NAME2,... uses CSV parsing */
} test_mode = TEST_MODE_NONE;

/* Selected test names (CSV list for tests=NAME1,NAME2,...) */
#define MAX_SELECTED_TESTS 16
static const char *selected_test_names[MAX_SELECTED_TESTS];
static int selected_test_count = 0;

/* Track which tests have already run (for unified mode) */
#define MAX_TESTS 16
static const char *tests_run_names[MAX_TESTS];  /* Stores names of run tests */
static int tests_run_count = 0;

/* ============================================================================
 * Test Registry
 * ============================================================================ */

#include "include/string.h"

/* External test functions */
extern int run_pmm_tests(void);
extern int run_slab_tests(void);
extern int run_spinlock_tests(void);
extern int run_nk_fault_injection_tests(void);
extern int run_apic_timer_tests(void);
extern int run_boot_tests(void);
extern int run_smp_tests(void);
extern int run_pcd_tests(void);
extern int run_nk_invariants_tests(void);
extern int run_nk_readonly_visibility_tests(void);
extern int run_usermode_tests(void);
extern int run_nk_smp_monitor_stress_tests(void);
extern int run_minilibc_tests(void);

/* Test registry array */
const test_case_t test_registry[] = {
#if CONFIG_TESTS_PMM
    {
        .name = "pmm",
        .description = "Physical memory manager allocation tests",
        .run_func = run_pmm_tests,
        .enabled = 1,
        .auto_run = 1  /* Auto-run after pmm_init() */
    },
#endif
#if CONFIG_TESTS_SLAB
    {
        .name = "slab",
        .description = "Slab allocator small object allocation tests",
        .run_func = run_slab_tests,
        .enabled = 1,
        .auto_run = 1  /* Auto-run after slab_init() */
    },
#endif
#if CONFIG_TESTS_SPINLOCK
    {
        .name = "spinlock",
        .description = "Spin lock and read-write lock synchronization tests",
        .run_func = run_spinlock_tests,
        .enabled = 1,
        .auto_run = 1  /* Auto-run after AP startup */
    },
#endif
#if CONFIG_TESTS_APIC_TIMER
    {
        .name = "timer",
        .description = "APIC timer interrupt-driven tests",
        .run_func = run_apic_timer_tests,
        .enabled = 1,
        .auto_run = 1  /* Auto-run after BSP init */
    },
#endif
#if CONFIG_TESTS_NK_FAULT_INJECTION
    {
        .name = "nk_fault_injection",
        .description = "Nested kernel fault injection tests (destructive)",
        .run_func = run_nk_fault_injection_tests,
        .enabled = 1,
        .auto_run = 1  /* Auto-run in test-all */
    },
#endif
#if CONFIG_TESTS_BOOT
    {
        .name = "boot",
        .description = "Basic kernel boot verification",
        .run_func = run_boot_tests,
        .enabled = 1,
        .auto_run = 1  /* Auto-run in test-all */
    },
#endif
#if CONFIG_TESTS_SMP
    {
        .name = "smp",
        .description = "SMP startup and multi-CPU verification",
        .run_func = run_smp_tests,
        .enabled = 1,
        .auto_run = 1  /* Auto-run in test-all */
    },
#endif
#if CONFIG_TESTS_PCD
    {
        .name = "pcd",
        .description = "Page Control Data initialization and tracking",
        .run_func = run_pcd_tests,
        .enabled = 1,
        .auto_run = 1  /* Auto-run in test-all */
    },
#endif
#if CONFIG_TESTS_NK_INVARIANTS
    {
        .name = "nk_invariants",
        .description = "Nested Kernel invariants (ASPLOS '15)",
        .run_func = run_nk_invariants_tests,
        .enabled = 1,
        .auto_run = 1  /* Auto-run in test-all */
    },
#endif
#if CONFIG_TESTS_NK_READONLY_VISIBILITY
    {
        .name = "nk_readonly_visibility",
        .description = "Read-only mapping visibility for nested kernel",
        .run_func = run_nk_readonly_visibility_tests,
        .enabled = 1,
        .auto_run = 1  /* Auto-run in test-all */
    },
#endif
#if CONFIG_TESTS_MINILIBC
    {
        .name = "minilibc",
        .description = "Minimal string library tests (49 tests: strlen, strcpy, strcmp, strncmp, memset, memcpy, snprintf)",
        .run_func = run_minilibc_tests,
        .enabled = 1,
        .auto_run = 1  /* Auto-run after kernel init */
    },
#endif
#if CONFIG_TESTS_USERMODE
    {
        .name = "usermode",
        .description = "User mode syscall and ring 3 execution tests",
        .run_func = run_usermode_tests,
        .enabled = 1,
        .auto_run = 1  /* Auto-run in test-all */
    },
#endif
#if CONFIG_TESTS_SMP_MONITOR_STRESS
    {
        .name = "nk_smp_monitor_stress",
        .description = "SMP monitor stress test - concurrent monitor calls from multiple CPUs",
        .run_func = run_nk_smp_monitor_stress_tests,
        .enabled = 1,
        .auto_run = 1  /* Auto-run in test-all */
    },
#endif
    { .name = NULL }  /* Sentinel */
};

/* ============================================================================
 * Framework Implementation
 * ============================================================================ */

/**
 * parse_test_csv() - Parse CSV list of test names
 * @csv_str: Comma-separated test names (e.g., "slab,minilibc,timer")
 *
 * Returns: Number of tests parsed
 */
static int parse_test_csv(const char *csv_str) {
    const char *p = csv_str;
    int count = 0;

    while (*p && count < MAX_SELECTED_TESTS) {
        /* Skip leading whitespace */
        while (*p == ' ' || *p == '\t') {
            p++;
        }

        if (*p == '\0') {
            break;
        }

        /* Found test name start */
        selected_test_names[count++] = p;

        /* Find end of test name (comma or null terminator) */
        while (*p && *p != ',') {
            p++;
        }

        /* Null-terminate the test name */
        if (*p == ',') {
            *(char *)p = '\0';
            p++;
        }
    }

    return count;
}

/**
 * test_framework_init() - Initialize test framework from cmdline
 */
void test_framework_init(void) {
    const char *tests_value;

    klog_info("TEST", "Initializing test framework...");

    /* Initialize tests_run_names tracking array */
    for (int i = 0; i < MAX_TESTS; i++) {
        tests_run_names[i] = NULL;
    }
    tests_run_count = 0;

    /* Initialize selected test names */
    for (int i = 0; i < MAX_SELECTED_TESTS; i++) {
        selected_test_names[i] = NULL;
    }
    selected_test_count = 0;

    /* Parse tests= parameter from cmdline */
    tests_value = cmdline_get_value("tests");

    if (tests_value == NULL) {
        test_mode = TEST_MODE_NONE;
        klog_info("TEST", "No tests= parameter (default: no tests run)");
        return;
    }

    klog_info("TEST", "tests=%s", tests_value);

    /* Determine test mode */
    if (strcmp(tests_value, "all") == 0) {
        test_mode = TEST_MODE_ALL;
        klog_info("TEST", "Mode: ALL (run all auto_run tests)");
    } else if (strcmp(tests_value, "unified") == 0) {
        test_mode = TEST_MODE_UNIFIED;
        klog_info("TEST", "Mode: UNIFIED (run all selected tests at end)");
    } else {
        /* Parse CSV list of test names */
        selected_test_count = parse_test_csv(tests_value);
        if (selected_test_count > 0) {
            klog_info("TEST", "Mode: CSV LIST (%d test(s))", selected_test_count);
            for (int i = 0; i < selected_test_count; i++) {
                klog_info("TEST", "  [%d/%d] %s", i + 1, selected_test_count, selected_test_names[i]);
            }
        } else {
            klog_error("TEST", "Empty or invalid CSV list");
            test_mode = TEST_MODE_NONE;
        }
    }
}

/**
 * test_should_run() - Check if test should run in auto mode
 * @name: Test name
 *
 * Returns: 1 if test should run, 0 otherwise
 */
int test_should_run(const char *name) {
    /* No tests= parameter: no tests run */
    if (test_mode == TEST_MODE_NONE) {
        return 0;
    }

    /* Unified mode: tests run at end, not during init */
    if (test_mode == TEST_MODE_UNIFIED) {
        return 0;  /* Will be run by test_run_unified() */
    }

    /* tests=all: run all enabled tests with auto_run=1 */
    if (test_mode == TEST_MODE_ALL) {
        /* Find test in registry and check auto_run flag */
        for (int i = 0; test_registry[i].name != NULL; i++) {
            if (strcmp(test_registry[i].name, name) == 0) {
                return test_registry[i].enabled && test_registry[i].auto_run;
            }
        }
        return 0;  /* Test not found */
    }

    /* CSV mode: check if name is in selected_test_names */
    if (selected_test_count > 0) {
        for (int i = 0; i < selected_test_count; i++) {
            if (strcmp(selected_test_names[i], name) == 0) {
                return 1;
            }
        }
        return 0;  /* Test name not in CSV list */
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

    /* Find test in registry */
    for (int i = 0; test_registry[i].name != NULL; i++) {
        if (strcmp(test_registry[i].name, name) == 0) {
            test = &test_registry[i];
            break;
        }
    }

    if (test == NULL) {
        klog_error("TEST", "Test '%s' not found in registry", name);
        return -1;
    }

    if (!test->enabled) {
        klog_error("TEST", "Test '%s' is not enabled (check CONFIG_ flag)", name);
        return -1;
    }

    /* Mark test as run */
    if (tests_run_count < MAX_TESTS) {
        tests_run_names[tests_run_count++] = name;
    }

    /* Run test */
    int result = test->run_func();

    /* Add separator for output clarity and ensure buffer sync */
    serial_puts("\n");
    serial_flush();

    if (result == 0) {
        klog_info("TEST", "PASSED: %s", name);
        serial_flush();
    } else {
        klog_error("TEST", "FAILED: %s (failures: %d)", name, result);
        serial_flush();
        klog_error("TEST", "FAILURE - System shutting down");
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

    /* Only run if in unified mode or CSV mode with selected tests */
    int should_run = 0;

    if (test_mode == TEST_MODE_UNIFIED) {
        should_run = 1;
    } else if (selected_test_count > 0) {
        /* CSV mode: run selected tests that haven't run yet */
        should_run = 1;
    }

    if (!should_run) {
        return 0;
    }

    serial_puts("\n");
    serial_puts("========================================\n");
    if (test_mode == TEST_MODE_UNIFIED) {
        serial_puts("UNIFIED TEST EXECUTION\n");
    } else {
        serial_puts("CSV TEST LIST EXECUTION\n");
    }
    serial_puts("========================================\n");
    serial_puts("\n");

    /* Run tests based on mode */
    if (test_mode == TEST_MODE_UNIFIED || test_mode == TEST_MODE_ALL) {
        /* Run all enabled tests that haven't run yet */
        for (int i = 0; test_registry[i].name != NULL; i++) {
            const test_case_t *test = &test_registry[i];

            if (!test->enabled) {
                continue;  /* Skip disabled tests */
            }

            /* Check if this specific test already ran */
            int already_run = 0;
            for (int j = 0; j < tests_run_count; j++) {
                if (tests_run_names[j] && strcmp(tests_run_names[j], test->name) == 0) {
                    already_run = 1;
                    break;
                }
            }
            if (already_run) {
                continue;
            }

            /* Run the test */
            int result = test->run_func();

            if (result != 0) {
                klog_error("TEST", "FAILED: %s", test->name);
                failures++;
                /* Continue running other tests to see all failures */
            } else {
                klog_info("TEST", "PASSED: %s", test->name);
            }
        }
    } else {
        /* CSV mode: run selected tests that haven't run yet */
        for (int i = 0; i < selected_test_count; i++) {
            const char *test_name = selected_test_names[i];
            const test_case_t *test = NULL;

            /* Find test in registry */
            for (int j = 0; test_registry[j].name != NULL; j++) {
                if (strcmp(test_registry[j].name, test_name) == 0) {
                        test = &test_registry[j];
                        break;
                }
            }

            if (test == NULL) {
                klog_error("TEST", "Test '%s' not found in registry", test_name);
                failures++;
                continue;
            }

            if (!test->enabled) {
                klog_error("TEST", "Test '%s' is not enabled", test_name);
                failures++;
                continue;
            }

            /* Check if this specific test already ran */
            int already_run = 0;
            for (int j = 0; j < tests_run_count; j++) {
                if (tests_run_names[j] && strcmp(tests_run_names[j], test_name) == 0) {
                    already_run = 1;
                    break;
                }
            }
            if (already_run) {
                klog_info("TEST", "SKIP: %s (already run)", test_name);
                continue;
            }

            /* Run the test */
            int result = test->run_func();

            if (result != 0) {
                klog_error("TEST", "FAILED: %s", test_name);
                failures++;
                /* Continue running other tests to see all failures */
            } else {
                klog_info("TEST", "PASSED: %s", test_name);
            }
        }
    }

    /* Summary */
    serial_puts("========================================\n");
    if (failures == 0) {
        klog_info("TEST", "UNIFIED: ALL TESTS PASSED");
    } else {
        klog_error("TEST", "UNIFIED: %d tests FAILED", failures);
    }
    serial_puts("========================================\n");
    serial_puts("\n");

    return failures;
}
