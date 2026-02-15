/* Emergence Kernel - SMP Test Wrapper Header */

#ifndef TEST_SMP_H
#define TEST_SMP_H

/**
 * test_smp - Run SMP startup and multi-CPU verification tests
 *
 * Wrapper function that handles CONFIG guard and runtime test selection.
 * Calls run_smp_tests() if CONFIG_TESTS_SMP is enabled and test is selected.
 */
void test_smp(void);

#endif /* TEST_SMP_H */
