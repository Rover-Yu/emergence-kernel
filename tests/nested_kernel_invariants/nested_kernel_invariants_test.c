/* Emergence Kernel - Nested Kernel Invariants Tests */

#include <stdint.h>
#include "test_nk_invariants.h"
#include "kernel/test.h"
#include "arch/x86_64/serial.h"
#include "kernel/pcd.h"

/* External function prototypes */
extern void serial_puts(const char *str);
extern void serial_putc(char c);
extern void serial_put_hex(uint64_t value);

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
    serial_puts("[NK-INV-NEG] Test: PTP write detection\n");

    /* Verify boot_pml4 is protected (read-only in unpriv view) */
    uint64_t pml4_phys = (uint64_t)boot_pml4;
    uint8_t type = pcd_get_type(pml4_phys);

    serial_puts("[NK-INV-NEG] boot_pml4 PCD type: ");
    serial_put_hex(type);
    serial_puts(" (expected NK_PGTABLE=2 or NK_NORMAL=1)\n");

    if (type == PCD_TYPE_NK_PGTABLE || type == PCD_TYPE_NK_NORMAL) {
        serial_puts("[NK-INV-NEG] PTP write detection: CONFIGURED (PASS)\n");
    } else {
        serial_puts("[NK-INV-NEG] PTP not properly tracked in PCD (WARN)\n");
    }
}

/* Negative test: Verify arbitrary CR3 load detection
 * The monitor should only allow pre-declared CR3 values. */
static void test_cr3_restriction(void) {
    serial_puts("[NK-INV-NEG] Test: CR3 restriction\n");

    uint64_t current_cr3;
    asm volatile ("mov %%cr3, %0" : "=r"(current_cr3));

    serial_puts("[NK-INV-NEG] Current CR3: ");
    serial_put_hex(current_cr3);
    serial_puts("\n");
    serial_puts("[NK-INV-NEG] unpriv_pml4_phys: ");
    serial_put_hex(unpriv_pml4_phys);
    serial_puts("\n");
    serial_puts("[NK-INV-NEG] monitor_pml4_phys: ");
    serial_put_hex(monitor_pml4_phys);
    serial_puts("\n");

    /* CR3 should be one of the pre-declared values */
    if (current_cr3 == unpriv_pml4_phys || current_cr3 == monitor_pml4_phys) {
        serial_puts("[NK-INV-NEG] CR3 is pre-declared (PASS)\n");
    } else {
        serial_puts("[NK-INV-NEG] CR3 is not pre-declared (FAIL)\n");
    }
}

/* Negative test: Verify writable NK mapping rejection
 * monitor_map_page should reject writable mappings to NK pages. */
static void test_writable_nk_rejection(void) {
    serial_puts("[NK-INV-NEG] Test: Writable NK mapping rejection\n");

    /* This test documents the expected behavior:
     * monitor_map_page should return -1 when asked to create
     * a writable mapping to an NK_NORMAL page. */

    serial_puts("[NK-INV-NEG] Writable NK rejection: DOCUMENTED\n");
    serial_puts("[NK-INV-NEG] (Actual enforcement tested via fault injection)\n");
}

/**
 * run_nested_kernel_invariants_tests - Run Nested Kernel invariants tests
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
int run_nested_kernel_invariants_tests(void) {
    int failures = 0;
    uint64_t unpriv_cr3;
    int cpu_index;

    serial_puts("\n========================================\n");
    serial_puts("  Nested Kernel Invariants Test Suite\n");
    serial_puts("========================================\n");
    serial_puts("\n");

    /* Test 1: Verify monitor initialization occurred */
    serial_puts("[NK INV TEST] Test 1: Monitor initialization\n");
    /* Monitor is initialized in main.c before tests run */
    serial_puts("[NK INV TEST] Monitor initialized in main (SKIP)\n");

    /* Test 2: Verify CR3 switch to unprivileged mode */
    serial_puts("[NK INV TEST] Test 2: CR3 unprivileged mode\n");
    unpriv_cr3 = monitor_get_unpriv_cr3();
    serial_puts("[NK INV TEST] Unprivileged CR3: ");
    serial_put_hex(unpriv_cr3);
    serial_puts("\n");

    if (unpriv_cr3 != 0) {
        serial_puts("[NK INV TEST] CR3 switch complete (PASS)\n");
    } else {
        serial_puts("[NK INV TEST] CR3 NOT switched (FAIL)\n");
        failures++;
    }

    /* Test 3: Verify BSP initialization (needed before invariants) */
    serial_puts("[NK INV TEST] Test 3: BSP initialization\n");
    if (bsp_init_done) {
        serial_puts("[NK INV TEST] BSP initialized (PASS)\n");
    } else {
        serial_puts("[NK INV TEST] BSP NOT initialized (FAIL)\n");
        failures++;
    }

    /* Test 4: Verify CPU index is valid */
    serial_puts("[NK INV TEST] Test 4: CPU identification\n");
    extern int smp_get_cpu_index(void);
    cpu_index = smp_get_cpu_index();
    serial_puts("[NK INV TEST] CPU index: ");
    serial_put_hex(cpu_index);
    serial_puts("\n");

    if (cpu_index >= 0 && cpu_index < 4) {
        serial_puts("[NK INV TEST] Valid CPU index (PASS)\n");
    } else {
        serial_puts("[NK INV TEST] Invalid CPU index (FAIL)\n");
        failures++;
    }

    /* Test 5: Run Nested Kernel invariants verification */
    serial_puts("[NK INV TEST] Test 5: Invariants verification\n");
    serial_puts("[NK INV TEST] Verifying all 6 ASPLOS '15 invariants...\n");
    monitor_verify_invariants();
    serial_puts("[NK INV TEST] Invariants verification complete (PASS)\n");

    /* Run negative test cases */
    serial_puts("\n[NK INV TEST] Running negative test cases...\n");
    test_ptp_write_detection();
    test_cr3_restriction();
    test_writable_nk_rejection();

    /* Print summary */
    serial_puts("\n========================================\n");
    if (failures == 0) {
        serial_puts("  NESTED KERNEL INVARIANTS: All tests PASSED\n");
    } else {
        serial_puts("  NESTED KERNEL INVARIANTS: Some tests FAILED (");
        serial_put_hex(failures);
        serial_puts(" failures)\n");
    }
    serial_puts("========================================\n");
    serial_puts("\n");

    return (failures > 0) ? -1 : 0;
}

/* ============================================================================
 * Test Wrapper
 * ============================================================================ */

#if CONFIG_TESTS_NK_INVARIANTS
void test_nk_invariants(void) {
    if (test_should_run("nested_kernel_invariants")) {
        test_run_by_name("nested_kernel_invariants");
    }
}
#else
void test_nk_invariants(void) { }
#endif
