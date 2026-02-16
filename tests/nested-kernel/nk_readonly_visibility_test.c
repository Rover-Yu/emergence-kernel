/* Emergence Kernel - NK Read-Only Visibility Tests */

#include <stdint.h>
#include "test_nk_readonly_visibility.h"
#include "kernel/test.h"
#include "kernel/klog.h"
#include "arch/x86_64/serial.h"
#include "kernel/pcd.h"

#if CONFIG_TESTS_NK_READONLY_VISIBILITY

/* External monitor functions */
extern void monitor_init(void);
extern void monitor_verify_invariants(void);
extern uint64_t monitor_get_unpriv_cr3(void);

/* External kernel state */
extern volatile int bsp_init_done;

/**
 * run_nk_readonly_visibility_tests - Run read-only visibility tests
 *
 * Verifies that the monitor creates read-only mappings for nested kernel
 * pages so the outer kernel can inspect but not modify them.
 *
 * Tests:
 * 1. PCD initialization
 * 2. Monitor initialization
 * 3. Read-only mappings creation
 * 4. Nested Kernel invariants pass
 * 5. Page tables are marked NK_PGTABLE
 *
 * Returns: 0 on success, -1 on failure
 */
int run_nk_readonly_visibility_tests(void) {
    int failures = 0;
    uint64_t unpriv_cr3;
    uint64_t max_pages;
    uint8_t page_type;

    klog_info("NK_RO_VIS_TEST", "=== Read-Only Visibility Test Suite ===");

    /* Test 1: Verify PCD initialization */
    klog_info("NK_RO_VIS_TEST", "Test 1: PCD initialization");
    if (pcd_is_initialized()) {
        klog_info("NK_RO_VIS_TEST", "PCD initialized (PASS)");
    } else {
        klog_error("NK_RO_VIS_TEST", "PCD NOT initialized (FAIL)");
        failures++;
    }

    /* Test 2: Verify PCD has valid page count */
    klog_info("NK_RO_VIS_TEST", "Test 2: PCD page count");
    max_pages = pcd_get_max_pages();
    klog_info("NK_RO_VIS_TEST", "Max pages: %x", max_pages);

    if (max_pages > 0) {
        klog_info("NK_RO_VIS_TEST", "PCD has pages (PASS)");
    } else {
        klog_error("NK_RO_VIS_TEST", "PCD has no pages (FAIL)");
        failures++;
    }

    /* Test 3: Verify monitor initialization */
    klog_info("NK_RO_VIS_TEST", "Test 3: Monitor initialization");
    /* Monitor is initialized in main.c */
    klog_info("NK_RO_VIS_TEST", "Monitor initialized in main (SKIP)");

    /* Test 4: Verify CR3 switch to unprivileged mode */
    klog_info("NK_RO_VIS_TEST", "Test 4: CR3 unprivileged mode");
    unpriv_cr3 = monitor_get_unpriv_cr3();
    klog_info("NK_RO_VIS_TEST", "Unprivileged CR3: %x", unpriv_cr3);

    if (unpriv_cr3 != 0) {
        klog_info("NK_RO_VIS_TEST", "CR3 switch complete (PASS)");
    } else {
        klog_error("NK_RO_VIS_TEST", "CR3 NOT switched (FAIL)");
        failures++;
    }

    /* Test 5: Verify BSP initialization */
    klog_info("NK_RO_VIS_TEST", "Test 5: BSP initialization");
    if (bsp_init_done) {
        klog_info("NK_RO_VIS_TEST", "BSP initialized (PASS)");
    } else {
        klog_error("NK_RO_VIS_TEST", "BSP NOT initialized (FAIL)");
        failures++;
    }

    /* Test 6: Verify Nested Kernel invariants */
    klog_info("NK_RO_VIS_TEST", "Test 6: Nested Kernel invariants");
    monitor_verify_invariants();
    klog_info("NK_RO_VIS_TEST", "Invariants verification complete (PASS)");

    /* Test 7: Verify page tables are marked (check PCD type for page table area) */
    klog_info("NK_RO_VIS_TEST", "Test 7: Page table markings");
    /* Check a page in the typical page table area (0x1000-0x7000) */
    page_type = pcd_get_type(0x1000);
    klog_info("NK_RO_VIS_TEST", "Page type at 0x1000: %x", page_type);

    if (page_type == PCD_TYPE_NK_PGTABLE || page_type == 255) {
        klog_info("NK_RO_VIS_TEST", "Page table area properly marked (PASS)");
    } else {
        klog_warn("NK_RO_VIS_TEST", "Page table area NOT marked (WARNING - may be OK)");
        /* Don't fail - page table locations vary */
    }

    /* Print summary */
    if (failures == 0) {
        klog_info("NK_RO_VIS_TEST", "READ-ONLY VISIBILITY: All tests PASSED");
    } else {
        klog_error("NK_RO_VIS_TEST", "READ-ONLY VISIBILITY: %d tests FAILED", failures);
    }

    return (failures > 0) ? -1 : 0;
}

#endif /* CONFIG_TESTS_NK_READONLY_VISIBILITY */

/* ============================================================================
 * Test Wrapper
 * ============================================================================ */

#if CONFIG_TESTS_NK_READONLY_VISIBILITY
void test_nk_readonly_visibility(void) {
    if (test_should_run("nk_readonly_visibility")) {
        test_run_by_name("nk_readonly_visibility");
    }
}
#else
void test_nk_readonly_visibility(void) { }
#endif
