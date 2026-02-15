/* Emergence Kernel - NK Invariants Test Wrapper Header */

#ifndef TEST_NK_INVARIANTS_H
#define TEST_NK_INVARIANTS_H

/**
 * test_nk_invariants - Run Nested Kernel invariants tests
 *
 * Wrapper function that handles CONFIG guard and runtime test selection.
 * Calls run_nested_kernel_invariants_tests() if CONFIG_TESTS_NK_INVARIANTS
 * is enabled and test is selected.
 */
void test_nk_invariants(void);

#endif /* TEST_NK_INVARIANTS_H */
