/* Emergence Kernel - KMAP Test Wrapper Header */

#ifndef TEST_KMAP_H
#define TEST_KMAP_H

/**
 * test_kmap - Run KMAP subsystem tests
 *
 * Tests kernel virtual memory mapping functionality including:
 * - Boot mappings verification
 * - Create/destroy with refcounting
 * - Lookup operations
 * - Split and merge operations
 * - SMP concurrent operations
 */
void test_kmap(void);

/**
 * kmap_test_ap_entry - AP entry point for SMP tests
 *
 * Called by APs to participate in SMP KMAP tests.
 */
void kmap_test_ap_entry(void);

#endif /* TEST_KMAP_H */
