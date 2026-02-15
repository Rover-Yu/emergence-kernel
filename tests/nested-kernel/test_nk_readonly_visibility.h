/* Emergence Kernel - NK Read-only Visibility Test Wrapper Header */

#ifndef TEST_NK_READONLY_VISIBILITY_H
#define TEST_NK_READONLY_VISIBILITY_H

/**
 * test_nk_readonly_visibility - Run read-only mapping visibility tests
 *
 * Wrapper function that handles CONFIG guard and runtime test selection.
 * Calls run_nk_readonly_visibility_tests() if CONFIG_TESTS_NK_READONLY_VISIBILITY
 * is enabled and test is selected.
 */
void test_nk_readonly_visibility(void);

#endif /* TEST_NK_READONLY_VISIBILITY_H */
