/* Emergence Kernel - Usermode Test Wrapper Header */

#ifndef TEST_USERMODE_H
#define TEST_USERMODE_H

/**
 * test_usermode_prepare - Prepare for usermode tests
 *
 * Handles special setup for usermode tests:
 * - Disables monitor init when running usermode test
 * - Pre-allocates user stack before CR3 switch
 *
 * Returns: 1 if usermode test is selected and preparation done, 0 otherwise
 */
int test_usermode_prepare(void);

/**
 * test_usermode - Run user mode syscall and ring 3 execution tests
 *
 * Wrapper function that handles CONFIG guard and runtime test selection.
 * Calls run_usermode_tests() if CONFIG_TESTS_USERMODE is enabled and test is selected.
 */
void test_usermode(void);

#endif /* TEST_USERMODE_H */
