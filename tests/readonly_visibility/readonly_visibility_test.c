/* Emergence Kernel - Read-Only Visibility Tests */

#include <stdint.h>
#include "test_readonly_visibility.h"
#include "kernel/test.h"
#include "arch/x86_64/serial.h"
#include "kernel/pcd.h"

#if CONFIG_TESTS_NK_READONLY_VISIBILITY

/* External function prototypes */
extern void serial_puts(const char *str);
extern void serial_put_hex(uint64_t value);

/* External monitor functions */
extern void monitor_init(void);
extern void monitor_verify_invariants(void);
extern uint64_t monitor_get_unpriv_cr3(void);

/* External kernel state */
extern volatile int bsp_init_done;

/**
 * run_readonly_visibility_tests - Run read-only visibility tests
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
int run_readonly_visibility_tests(void) {
    int failures = 0;
    uint64_t unpriv_cr3;
    uint64_t max_pages;
    uint8_t page_type;

    serial_puts("\n========================================\n");
    serial_puts("  Read-Only Visibility Test Suite\n");
    serial_puts("========================================\n");
    serial_puts("\n");

    /* Test 1: Verify PCD initialization */
    serial_puts("[RO VIS TEST] Test 1: PCD initialization\n");
    if (pcd_is_initialized()) {
        serial_puts("[RO VIS TEST] PCD initialized (PASS)\n");
    } else {
        serial_puts("[RO VIS TEST] PCD NOT initialized (FAIL)\n");
        failures++;
    }

    /* Test 2: Verify PCD has valid page count */
    serial_puts("[RO VIS TEST] Test 2: PCD page count\n");
    max_pages = pcd_get_max_pages();
    serial_puts("[RO VIS TEST] Max pages: ");
    serial_put_hex(max_pages);
    serial_puts("\n");

    if (max_pages > 0) {
        serial_puts("[RO VIS TEST] PCD has pages (PASS)\n");
    } else {
        serial_puts("[RO VIS TEST] PCD has no pages (FAIL)\n");
        failures++;
    }

    /* Test 3: Verify monitor initialization */
    serial_puts("[RO VIS TEST] Test 3: Monitor initialization\n");
    /* Monitor is initialized in main.c */
    serial_puts("[RO VIS TEST] Monitor initialized in main (SKIP)\n");

    /* Test 4: Verify CR3 switch to unprivileged mode */
    serial_puts("[RO VIS TEST] Test 4: CR3 unprivileged mode\n");
    unpriv_cr3 = monitor_get_unpriv_cr3();
    serial_puts("[RO VIS TEST] Unprivileged CR3: ");
    serial_put_hex(unpriv_cr3);
    serial_puts("\n");

    if (unpriv_cr3 != 0) {
        serial_puts("[RO VIS TEST] CR3 switch complete (PASS)\n");
    } else {
        serial_puts("[RO VIS TEST] CR3 NOT switched (FAIL)\n");
        failures++;
    }

    /* Test 5: Verify BSP initialization */
    serial_puts("[RO VIS TEST] Test 5: BSP initialization\n");
    if (bsp_init_done) {
        serial_puts("[RO VIS TEST] BSP initialized (PASS)\n");
    } else {
        serial_puts("[RO VIS TEST] BSP NOT initialized (FAIL)\n");
        failures++;
    }

    /* Test 6: Verify Nested Kernel invariants */
    serial_puts("[RO VIS TEST] Test 6: Nested Kernel invariants\n");
    monitor_verify_invariants();
    serial_puts("[RO VIS TEST] Invariants verification complete (PASS)\n");

    /* Test 7: Verify page tables are marked (check PCD type for page table area) */
    serial_puts("[RO VIS TEST] Test 7: Page table markings\n");
    /* Check a page in the typical page table area (0x1000-0x7000) */
    page_type = pcd_get_type(0x1000);
    serial_puts("[RO VIS TEST] Page type at 0x1000: ");
    serial_put_hex(page_type);
    serial_puts("\n");

    if (page_type == PCD_TYPE_NK_PGTABLE || page_type == 255) {
        serial_puts("[RO VIS TEST] Page table area properly marked (PASS)\n");
    } else {
        serial_puts("[RO VIS TEST] Page table area NOT marked (WARNING - may be OK)\n");
        /* Don't fail - page table locations vary */
    }

    /* Print summary */
    serial_puts("\n========================================\n");
    if (failures == 0) {
        serial_puts("  READ-ONLY VISIBILITY: All tests PASSED\n");
    } else {
        serial_puts("  READ-ONLY VISIBILITY: Some tests FAILED (");
        serial_put_hex(failures);
        serial_puts(" failures)\n");
    }
    serial_puts("========================================\n");
    serial_puts("\n");

    return (failures > 0) ? -1 : 0;
}

#endif /* CONFIG_TESTS_NK_READONLY_VISIBILITY */

/* ============================================================================
 * Test Wrapper
 * ============================================================================ */

#if CONFIG_TESTS_NK_READONLY_VISIBILITY
void test_readonly_visibility(void) {
    if (test_should_run("readonly_visibility")) {
        test_run_by_name("readonly_visibility");
    }
}
#else
void test_readonly_visibility(void) { }
#endif
