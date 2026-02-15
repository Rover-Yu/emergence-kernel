/* Emergence Kernel - Timer Test Wrapper Header */

#ifndef TEST_TIMER_H
#define TEST_TIMER_H

/**
 * test_timer - Run APIC timer interrupt-driven tests
 *
 * Wrapper function that handles CONFIG guard and runtime test selection.
 * Calls run_apic_timer_tests() if CONFIG_TESTS_APIC_TIMER is enabled and test is selected.
 */
void test_timer(void);

#endif /* TEST_TIMER_H */
