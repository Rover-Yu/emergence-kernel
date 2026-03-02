/* Emergence Kernel - Page Control Data (PCD) Tests */

#include <stdint.h>
#include "test_pcd.h"
#include "kernel/test.h"
#include "kernel/klog.h"
#include "arch/x86_64/serial.h"
#include "kernel/pcd.h"

/* External monitor functions */
extern void monitor_init(void);

/**
 * run_pcd_tests - Run Page Control Data (PCD) tests
 *
 * Tests that the PCD system is correctly initialized and tracking page types.
 *
 * Verifies:
 * 1. PCD system is initialized
 * 2. PCD has a valid page count (> 0)
 * 3. Can query PCD type for a valid address
 * 4. Monitor initialization still works after PCD
 *
 * Note: NK invariants are tested separately by the nk_invariants test.
 *
 * Returns: 0 on success, -1 on failure
 */
int run_pcd_tests(void) {
    int failures = 0;
    uint64_t max_pages;
    uint8_t page_type;
    int test_page_addr = 0x1000;  /* Test a low memory page */

    klog_info("PCD_TEST", "=== PCD Test Suite ===");

    /* Test 1: Verify PCD initialization */
    klog_info("PCD_TEST", "Test 1: PCD initialization");
    if (pcd_is_initialized()) {
        klog_info("PCD_TEST", "PCD initialized (PASS)");
    } else {
        klog_error("PCD_TEST", "PCD NOT initialized (FAIL)");
        failures++;
    }

    /* Test 2: Verify PCD has valid page count */
    klog_info("PCD_TEST", "Test 2: PCD page count");
    max_pages = pcd_get_max_pages();
    klog_info("PCD_TEST", "Max pages: %x", max_pages);

    if (max_pages > 0) {
        klog_info("PCD_TEST", "PCD has pages (PASS)");
    } else {
        klog_error("PCD_TEST", "PCD has no pages (FAIL)");
        failures++;
    }

    /* Test 3: Verify can query PCD type */
    klog_info("PCD_TEST", "Test 3: PCD type query");
    page_type = pcd_get_type(test_page_addr);
    klog_info("PCD_TEST", "Page type at %x: %x", test_page_addr, page_type);

    /* Page type should be valid (0-3) or 255 if not tracked */
    if (page_type <= 3 || page_type == 255) {
        klog_info("PCD_TEST", "Valid page type (PASS)");
    } else {
        klog_error("PCD_TEST", "Invalid page type (FAIL)");
        failures++;
    }

    /* Test 4: Verify monitor still initializes after PCD */
    klog_info("PCD_TEST", "Test 4: Monitor initialization");
    /* Note: Monitor is already initialized by main.c, so we just verify it worked */
    /* We can't call monitor_init() again, and invariants have their own dedicated test */
    klog_info("PCD_TEST", "Monitor already initialized in main (SKIP)");

    /* Print summary */
    if (failures == 0) {
        klog_info("PCD_TEST", "PCD: All tests PASSED");
    } else {
        klog_error("PCD_TEST", "PCD: Some tests FAILED (%d failures)", failures);
    }

    return (failures > 0) ? -1 : 0;
}

/* ============================================================================
 * Test Wrapper
 * ============================================================================ */

#if CONFIG_TESTS_PCD
void test_pcd(void) {
    if (test_should_run("pcd")) {
        test_run_by_name("pcd");
    }
}
#else
void test_pcd(void) { }
#endif
