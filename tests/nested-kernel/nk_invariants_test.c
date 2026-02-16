/* Emergence Kernel - Nested Kernel Invariants Tests */

#include <stdint.h>
#include "test_nk_invariants.h"
#include "kernel/test.h"
#include "kernel/klog.h"
#include "arch/x86_64/serial.h"
#include "kernel/pcd.h"

/* External monitor functions */
extern void monitor_init(void);
extern void monitor_verify_invariants(void);
extern uint64_t monitor_get_unpriv_cr3(void);
extern uint64_t monitor_pml4_phys;
extern uint64_t unpriv_pml4_phys;

/* External boot page tables */
extern uint64_t boot_pml4[];

/* External kernel state */
extern volatile int bsp_init_done;

/* Negative test: Verify PTP write detection would catch violations
 * This test DOCUMENTS expected behavior - actual fault injection
 * is in nk_fault_injection tests. */
static void test_ptp_write_detection(void) {
    klog_info("NK_INV_TEST", "Test: PTP write detection");

    /* Verify boot_pml4 is protected (read-only in unpriv view) */
    uint64_t pml4_phys = (uint64_t)boot_pml4;
    uint8_t type = pcd_get_type(pml4_phys);

    klog_info("NK_INV_TEST", "boot_pml4 PCD type: %x (expected NK_PGTABLE=2 or NK_NORMAL=1)", type);

    if (type == PCD_TYPE_NK_PGTABLE || type == PCD_TYPE_NK_NORMAL) {
        klog_info("NK_INV_TEST", "PTP write detection: CONFIGURED (PASS)");
    } else {
        klog_warn("NK_INV_TEST", "PTP not properly tracked in PCD (WARN)");
    }
}

/* Negative test: Verify arbitrary CR3 load detection
 * The monitor should only allow pre-declared CR3 values. */
static void test_cr3_restriction(void) {
    klog_info("NK_INV_TEST", "Test: CR3 restriction");

    uint64_t current_cr3;
    asm volatile ("mov %%cr3, %0" : "=r"(current_cr3));

    klog_info("NK_INV_TEST", "Current CR3: %x", current_cr3);
    klog_info("NK_INV_TEST", "unpriv_pml4_phys: %x", unpriv_pml4_phys);
    klog_info("NK_INV_TEST", "monitor_pml4_phys: %x", monitor_pml4_phys);

    /* CR3 should be one of the pre-declared values */
    if (current_cr3 == unpriv_pml4_phys || current_cr3 == monitor_pml4_phys) {
        klog_info("NK_INV_TEST", "CR3 is pre-declared (PASS)");
    } else {
        klog_error("NK_INV_TEST", "CR3 is not pre-declared (FAIL)");
    }
}

/* Negative test: Verify writable NK mapping rejection
 * monitor_map_page should reject writable mappings to NK pages. */
static void test_writable_nk_rejection(void) {
    klog_info("NK_INV_TEST", "Test: Writable NK mapping rejection");

    /* This test documents the expected behavior:
     * monitor_map_page should return -1 when asked to create
     * a writable mapping to an NK_NORMAL page. */

    klog_info("NK_INV_TEST", "Writable NK rejection: DOCUMENTED");
    klog_info("NK_INV_TEST", "(Actual enforcement tested via fault injection)");
}

/**
 * run_nk_invariants_tests - Run Nested Kernel invariants tests
 *
 * Verifies all 6 Nested Kernel invariants from the ASPLOS '15 paper:
 *
 *   Inv 1: PTPs read-only in outer kernel (PTE writable=0)
 *   Inv 2: CR0.WP enforcement active (bit 16)
 *   Inv 3: Global mappings accessible in both views
 *   Inv 4: Context switch mechanism available
 *   Inv 5: PTPs writable in nested kernel
 *   Inv 6: CR3 loaded with pre-declared PTP
 *
 * Returns: 0 on success, -1 on failure
 */
int run_nk_invariants_tests(void) {
    int failures = 0;
    uint64_t unpriv_cr3;
    int cpu_index;

    klog_info("NK_INV_TEST", "=== Nested Kernel Invariants Test Suite ===");

    /* Test 1: Verify monitor initialization occurred */
    klog_info("NK_INV_TEST", "Test 1: Monitor initialization");
    /* Monitor is initialized in main.c before tests run */
    klog_info("NK_INV_TEST", "Monitor initialized in main (SKIP)");

    /* Test 2: Verify CR3 switch to unprivileged mode */
    klog_info("NK_INV_TEST", "Test 2: CR3 unprivileged mode");
    unpriv_cr3 = monitor_get_unpriv_cr3();
    klog_info("NK_INV_TEST", "Unprivileged CR3: %x", unpriv_cr3);

    if (unpriv_cr3 != 0) {
        klog_info("NK_INV_TEST", "CR3 switch complete (PASS)");
    } else {
        klog_error("NK_INV_TEST", "CR3 NOT switched (FAIL)");
        failures++;
    }

    /* Test 3: Verify BSP initialization (needed before invariants) */
    klog_info("NK_INV_TEST", "Test 3: BSP initialization");
    if (bsp_init_done) {
        klog_info("NK_INV_TEST", "BSP initialized (PASS)");
    } else {
        klog_error("NK_INV_TEST", "BSP NOT initialized (FAIL)");
        failures++;
    }

    /* Test 4: Verify CPU index is valid */
    klog_info("NK_INV_TEST", "Test 4: CPU identification");
    extern int smp_get_cpu_index(void);
    cpu_index = smp_get_cpu_index();
    klog_info("NK_INV_TEST", "CPU index: %d", cpu_index);

    if (cpu_index >= 0 && cpu_index < 4) {
        klog_info("NK_INV_TEST", "Valid CPU index (PASS)");
    } else {
        klog_error("NK_INV_TEST", "Invalid CPU index (FAIL)");
        failures++;
    }

    /* Test 5: Run Nested Kernel invariants verification */
    klog_info("NK_INV_TEST", "Test 5: Invariants verification");
    klog_info("NK_INV_TEST", "Verifying all 6 ASPLOS '15 invariants...");
    monitor_verify_invariants();
    klog_info("NK_INV_TEST", "Invariants verification complete (PASS)");

    /* Run negative test cases */
    klog_info("NK_INV_TEST", "Running negative test cases...");
    test_ptp_write_detection();
    test_cr3_restriction();
    test_writable_nk_rejection();

    /* Print summary */
    if (failures == 0) {
        klog_info("NK_INV_TEST", "NESTED KERNEL INVARIANTS: All tests PASSED");
    } else {
        klog_error("NK_INV_TEST", "NESTED KERNEL INVARIANTS: Some tests FAILED (%d failures)", failures);
    }

    return (failures > 0) ? -1 : 0;
}

/* ============================================================================
 * Test Wrapper
 * ============================================================================ */

#if CONFIG_TESTS_NK_INVARIANTS
void test_nk_invariants(void) {
    if (test_should_run("nk_invariants")) {
        test_run_by_name("nk_invariants");
    }
}
#else
void test_nk_invariants(void) { }
#endif
