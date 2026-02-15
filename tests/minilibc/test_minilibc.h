/* Emergence Kernel - Minilibc Test Wrapper Header */

#ifndef TEST_MINILIBC_H
#define TEST_MINILIBC_H

/**
 * test_minilibc - Run minilibc string/memory function tests
 *
 * Wrapper function that handles CONFIG guard and runtime test selection.
 * Calls run_minilibc_tests() if CONFIG_TESTS_MINILIBC is enabled and test is selected.
 */
void test_minilibc(void);

#endif /* TEST_MINILIBC_H */
