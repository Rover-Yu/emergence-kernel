/* Emergence Kernel - SMP Monitor Stress Test Wrapper Header */

#ifndef TEST_SMP_MONITOR_STRESS_H
#define TEST_SMP_MONITOR_STRESS_H

/**
 * test_smp_monitor_stress - Run SMP monitor stress tests
 *
 * Wrapper function that handles CONFIG guard and runtime test selection.
 * Calls run_smp_monitor_stress_tests() if CONFIG_TESTS_SMP_MONITOR_STRESS
 * is enabled and test is selected.
 *
 * This test must be run from BSP after APs are ready.
 */
void test_smp_monitor_stress(void);

/**
 * test_smp_monitor_stress_ap_entry - AP entry point for SMP monitor stress test
 *
 * Called by APs when smp_monitor_stress_test_start is set.
 * APs participate in stress testing monitor calls.
 */
void test_smp_monitor_stress_ap_entry(void);

#endif /* TEST_SMP_MONITOR_STRESS_H */
