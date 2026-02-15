/* Emergence Kernel - Boot Test Wrapper Header */

#ifndef TEST_BOOT_H
#define TEST_BOOT_H

/**
 * test_boot - Run basic kernel boot verification tests
 *
 * Wrapper function that handles CONFIG guard and runtime test selection.
 * Calls run_boot_tests() if CONFIG_TESTS_BOOT is enabled and test is selected.
 */
void test_boot(void);

#endif /* TEST_BOOT_H */
