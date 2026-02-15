/* Emergence Kernel - NK Invariants Verify Test Wrapper Header */

#ifndef TEST_NK_INVARIANTS_VERIFY_H
#define TEST_NK_INVARIANTS_VERIFY_H

/**
 * test_nk_invariants_verify - Verify NK invariants on BSP after CR3 switch
 *
 * Wrapper function that handles CONFIG guard.
 * Calls monitor_verify_invariants() if CONFIG_TESTS_NK_INVARIANTS_VERIFY is enabled.
 */
void test_nk_invariants_verify(void);

/**
 * test_nk_invariants_verify_ap - Verify NK invariants on AP after CR3 switch
 *
 * Wrapper function that handles CONFIG guard.
 * Called by APs after switching to unprivileged page tables.
 */
void test_nk_invariants_verify_ap(void);

#endif /* TEST_NK_INVARIANTS_VERIFY_H */
