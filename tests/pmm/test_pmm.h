/* Emergence Kernel - PMM Test Wrapper Header */

#ifndef TEST_PMM_H
#define TEST_PMM_H

/**
 * test_pmm_early - Run early PMM tests (after pmm_init, before monitor)
 *
 * Wrapper function that handles CONFIG guard and runtime test selection.
 * Uses pmm_alloc() directly.
 */
void test_pmm_early(void);

/**
 * test_pmm_via_monitor - Run PMM tests via monitor (after monitor_init)
 *
 * Wrapper function that handles CONFIG guard and runtime test selection.
 * Uses monitor_pmm_alloc() for proper PCD enforcement.
 */
void test_pmm_via_monitor(void);

#endif /* TEST_PMM_H */
