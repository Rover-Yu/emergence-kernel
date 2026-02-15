/* Emergence Kernel - PCD Test Wrapper Header */

#ifndef TEST_PCD_H
#define TEST_PCD_H

/**
 * test_pcd - Run Page Control Data initialization and tracking tests
 *
 * Wrapper function that handles CONFIG guard and runtime test selection.
 * Calls run_pcd_tests() if CONFIG_TESTS_PCD is enabled and test is selected.
 */
void test_pcd(void);

#endif /* TEST_PCD_H */
