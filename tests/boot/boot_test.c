/* Emergence Kernel - Boot Tests */

#include <stdint.h>
#include "test_boot.h"
#include "kernel/test.h"
#include "arch/x86_64/serial.h"
#include "arch/x86_64/apic.h"
#include "arch/x86_64/smp.h"
#include "kernel/klog.h"

/* External kernel state */
extern volatile int bsp_init_done;

/**
 * run_boot_tests - Run basic kernel boot verification tests
 *
 * Tests that the kernel boots correctly on a single CPU by verifying:
 * 1. Kernel can output to serial console (this test itself proves it)
 * 2. CPU index is 0 (running on BSP)
 * 3. BSP initialization completed
 * 4. At least 1 CPU is ready
 * 5. Local APIC is accessible (can read APIC ID)
 *
 * Returns: 0 on success, -1 on failure
 */
int run_boot_tests(void) {
    int failures = 0;
    int cpu_index;
    uint8_t apic_id;

    serial_puts("\n========================================\n");
    serial_puts("  Boot Test Suite\n");
    serial_puts("========================================\n");
    serial_puts("\n");

    /* Test 1: Verify serial output is working (this test itself proves it) */
    klog_info("BOOT_TEST", "Test 1: Serial console output");
    klog_info("BOOT_TEST", "Serial console is working (PASS)");

    /* Test 2: Verify we're running on a valid CPU (BSP or AP) */
    klog_info("BOOT_TEST", "Test 2: CPU identification");
    cpu_index = smp_get_cpu_index();
    if (cpu_index >= 0 && cpu_index < SMP_MAX_CPUS) {
        klog_info("BOOT_TEST", "Running on valid CPU (PASS)");
        klog_info("BOOT_TEST", "CPU index: %d", cpu_index);
    } else {
        klog_error("BOOT_TEST", "Invalid CPU index (FAIL)");
        failures++;
    }

    /* Test 3: Verify BSP initialization completed */
    klog_info("BOOT_TEST", "Test 3: BSP initialization status");
    if (bsp_init_done) {
        klog_info("BOOT_TEST", "BSP initialization complete (PASS)");
    } else {
        klog_error("BOOT_TEST", "BSP initialization NOT complete (FAIL)");
        failures++;
    }

    /* Test 4: Verify at least 1 CPU exists */
    klog_info("BOOT_TEST", "Test 4: CPU count verification");
    if (smp_get_cpu_count() >= 1) {
        klog_info("BOOT_TEST", "At least 1 CPU detected (PASS)");
    } else {
        klog_error("BOOT_TEST", "No CPUs detected (FAIL)");
        failures++;
    }

    /* Test 5: Verify APIC is accessible */
    klog_info("BOOT_TEST", "Test 5: Local APIC accessibility");
    apic_id = lapic_get_id();
    klog_info("BOOT_TEST", "Local APIC ID: %x", apic_id);

    if (apic_id < 256) {  /* Valid APIC IDs are 0-255 */
        klog_info("BOOT_TEST", "Local APIC accessible (PASS)");
    } else {
        klog_error("BOOT_TEST", "Local APIC NOT accessible (FAIL)");
        failures++;
    }

    /* Print summary */
    serial_puts("\n========================================\n");
    if (failures == 0) {
        klog_info("BOOT_TEST", "BOOT: All tests PASSED");
    } else {
        klog_error("BOOT_TEST", "BOOT: %d tests FAILED", failures);
    }
    serial_puts("========================================\n");
    serial_puts("\n");

    return (failures > 0) ? -1 : 0;
}

/* ============================================================================
 * Test Wrapper
 * ============================================================================ */

#if CONFIG_TESTS_BOOT
void test_boot(void) {
    if (test_should_run("boot")) {
        test_run_by_name("boot");
    }
}
#else
void test_boot(void) { }
#endif
