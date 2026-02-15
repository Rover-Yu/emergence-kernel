/* Emergence Kernel - NK Fault Injection Test Wrapper Header */

#ifndef TEST_NK_FAULT_INJECTION_H
#define TEST_NK_FAULT_INJECTION_H

/**
 * test_nk_fault_injection - Run nested kernel fault injection tests
 *
 * Wrapper function that handles CONFIG guard and runtime test selection.
 * Calls run_nk_fault_injection_tests() if CONFIG_TESTS_NK_FAULT_INJECTION
 * is enabled and test is selected.
 *
 * WARNING: These tests are destructive and cause intentional page faults.
 */
void test_nk_fault_injection(void);

#endif /* TEST_NK_FAULT_INJECTION_H */
