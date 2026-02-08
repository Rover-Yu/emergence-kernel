/* Emergence Kernel - Test Framework Public API */

#ifndef _KERNEL_TEST_H
#define _KERNEL_TEST_H

#include <stdint.h>

/**
 * Test case structure
 *
 * Each test case is registered in the test_registry array.
 * The framework uses this structure to determine which tests to run
 * based on the kernel command line "test=" parameter.
 */
typedef struct test_case {
    const char *name;           /* Test name (e.g., "spinlock", "slab") */
    const char *description;    /* Human-readable description */
    int (*run_func)(void);      /* Test function pointer */
    int enabled;                /* Compile-time enable flag (CONFIG_TEST_*) */
    int auto_run;               /* Auto-run after subsystem init (1=auto, 0=manual only) */
} test_case_t;

/* Test registry - defined in kernel/test.c */
extern const test_case_t test_registry[];

/**
 * test_framework_init() - Parse test= parameter from kernel cmdline
 *
 * Reads test=<name|all|unified> from cmdline and stores selection.
 * Must be called once after multiboot_get_cmdline().
 *
 * Valid values:
 * - test=<name>  : Run specific test (e.g., test=spinlock)
 * - test=all     : Run all enabled tests with auto_run=1
 * - test=unified : Run all selected tests at end of init
 * - (no test=)   : No tests run (default behavior)
 */
void test_framework_init(void);

/**
 * test_should_run() - Check if test should run in auto mode
 * @name: Test name (e.g., "spinlock", "slab")
 *
 * Returns: 1 if test should run, 0 otherwise
 *
 * Returns 1 if:
 * - test=all is set AND test is enabled and auto_run=1
 * - test=<name> matches this test name
 * Returns 0 if no test= parameter or test not selected
 */
int test_should_run(const char *name);

/**
 * test_run_by_name() - Execute test by name with fail-fast
 * @name: Test name
 *
 * Executes the test and calls system_shutdown() on failure.
 *
 * Returns: 0 on success, non-zero on failure (but shutdown occurs first)
 */
int test_run_by_name(const char *name);

/**
 * test_run_unified() - Run all selected tests at unified point
 *
 * Executes all selected tests that haven't run yet.
 * Called at end of BSP init when test=unified is specified.
 *
 * Returns: 0 on success, non-zero if any test failed
 */
int test_run_unified(void);

#endif /* _KERNEL_TEST_H */
