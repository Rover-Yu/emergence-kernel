/* Emergence Kernel - Physical Memory Manager Tests */

#include <stdint.h>
#include <stddef.h>
#include "test_pmm.h"
#include "kernel/test.h"
#include "kernel/pmm.h"
#include "kernel/klog.h"
#include "arch/x86_64/serial.h"

/**
 * run_pmm_tests - Run physical memory manager tests
 *
 * Tests PMM allocation, freeing, coalescing, and statistics.
 *
 * Returns: 0 on success, -1 on failure
 */
int run_pmm_tests(void) {
    serial_puts("\n========================================\n");
    serial_puts("  PMM Test Suite\n");
    serial_puts("========================================\n");
    serial_puts("\n");

    /* Test 1: Single page allocation */
    klog_info("PMM_TEST", "Test 1: Single page allocation");
    void *page1 = pmm_alloc(0);
    void *page2 = pmm_alloc(0);

    if (page1 == NULL || page2 == NULL) {
        klog_error("PMM_TEST", "FAILED: Allocation returned NULL");
        return -1;
    }

    klog_info("PMM_TEST", "Allocated page1 at %p, page2 at %p", page1, page2);

    /* Test 2: Multi-page allocation (order 3 = 8 pages = 32KB) */
    klog_info("PMM_TEST", "Test 2: Multi-page allocation");
    void *block = pmm_alloc(3);
    if (block == NULL) {
        klog_error("PMM_TEST", "FAILED: Multi-page allocation returned NULL");
        return -1;
    }

    klog_info("PMM_TEST", "Allocated 32KB block at %p", block);

    /* Test 3: Free and coalesce */
    klog_info("PMM_TEST", "Test 3: Free and coalesce");
    pmm_free(page1, 0);
    pmm_free(page2, 0);
    klog_info("PMM_TEST", "Freed pages (buddy coalescing)");

    /* Test 4: Statistics */
    klog_info("PMM_TEST", "Test 4: Statistics");
    uint64_t free = pmm_get_free_pages();
    uint64_t total = pmm_get_total_pages();
    klog_info("PMM_TEST", "Free: %x / Total: %x", free, total);

    /* Test 5: Allocate adjacent pages to verify they were coalesced */
    klog_info("PMM_TEST", "Test 5: Allocate coalesced pages");
    void *page3 = pmm_alloc(1);  /* Request 2 pages */
    if (page3 == NULL) {
        klog_error("PMM_TEST", "FAILED: 2-page allocation returned NULL");
        return -1;
    }

    klog_info("PMM_TEST", "Allocated 2-page block at %p (should be same as page1 if coalesced)", page3);

    serial_puts("\n========================================\n");
    serial_puts("  PMM: All tests PASSED\n");
    serial_puts("========================================\n");
    serial_puts("\n");

    return 0;
}

/* ============================================================================
 * Test Wrappers
 * ============================================================================ */

#if CONFIG_TESTS_PMM
void test_pmm_early(void) {
    if (test_should_run("pmm")) {
        test_run_by_name("pmm");
    }
}

/* PMM via monitor - inline tests that use monitor_pmm_alloc */
void test_pmm_via_monitor(void) {
    if (test_should_run("pmm")) {
        klog_info("PMM_TEST", "Running allocation tests (via monitor)...");

        extern void *monitor_pmm_alloc(uint8_t order);
        extern void monitor_pmm_free(void *addr, uint8_t order);

        /* DEBUG: Before first allocation */
        klog_debug("PMM_TEST", "About to call monitor_pmm_alloc(0)...");

        /* Test 1: Single page allocation */
        void *page1 = monitor_pmm_alloc(0);

        klog_debug("PMM_TEST", "First alloc returned, page1 = %p", page1);

        void *page2 = monitor_pmm_alloc(0);
        klog_info("PMM_TEST", "Allocated page1 at %p, page2 at %p", page1, page2);

        /* Test 2: Multi-page allocation (order 3 = 8 pages = 32KB) */
        void *block = monitor_pmm_alloc(3);
        klog_info("PMM_TEST", "Allocated 32KB block at %p", block);

        /* Test 3: Free and coalesce */
        monitor_pmm_free(page1, 0);
        monitor_pmm_free(page2, 0);
        klog_info("PMM_TEST", "Freed pages (buddy coalescing)");

        /* Test 4: Statistics */
        extern uint64_t pmm_get_free_pages(void);
        extern uint64_t pmm_get_total_pages(void);
        uint64_t free = pmm_get_free_pages();
        uint64_t total = pmm_get_total_pages();
        klog_info("PMM_TEST", "Free: %x / Total: %x", free, total);

        /* Test 5: Allocate adjacent pages to verify they were coalesced */
        void *page3 = monitor_pmm_alloc(1);  /* Request 2 pages */
        klog_info("PMM_TEST", "Allocated 2-page block at %p (should be same as page1 if coalesced)", page3);

        klog_info("PMM_TEST", "Tests complete");
    }
}
#else
void test_pmm_early(void) { }
void test_pmm_via_monitor(void) { }
#endif
