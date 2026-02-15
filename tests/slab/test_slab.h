/* Emergence Kernel - Slab Test Wrapper Header */

#ifndef TEST_SLAB_H
#define TEST_SLAB_H

/**
 * test_slab - Run slab allocator tests
 *
 * Wrapper function that handles CONFIG guard and runtime test selection.
 * Calls run_slab_tests() if CONFIG_TESTS_SLAB is enabled and test is selected.
 */
void test_slab(void);

#endif /* TEST_SLAB_H */
