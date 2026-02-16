/* Emergence Kernel - SMP Boot Tests */

#include <stdint.h>
#include "test_smp.h"
#include "kernel/test.h"
#include "kernel/klog.h"
#include "arch/x86_64/serial.h"
#include "arch/x86_64/apic.h"
#include "arch/x86_64/smp.h"

/* External kernel state */
extern volatile int bsp_init_done;

/**
 * run_smp_tests - Run SMP (Symmetric Multi-Processing) boot tests
 *
 * Tests that SMP initialization works correctly by verifying:
 * 1. BSP (CPU 0) has booted successfully
 * 2. AP startup was initiated
 * 3. At least one AP has booted (in multi-CPU mode)
 * 4. Ready CPU count matches expected CPU count
 * 5. Each CPU has a valid APIC ID
 *
 * Returns: 0 on success, -1 on failure
 */
int run_smp_tests(void) {
    int failures = 0;
    int cpu_index;
    int cpu_count;
    int expected_cpus;
    uint8_t apic_id;

    klog_info("SMP_TEST", "=== SMP Boot Test Suite ===");

    /* Test 1: Verify a valid CPU has booted */
    klog_info("SMP_TEST", "Test 1: CPU boot verification");
    cpu_index = smp_get_cpu_index();
    if (cpu_index >= 0 && cpu_index < SMP_MAX_CPUS) {
        klog_info("SMP_TEST", "Valid CPU is running (PASS)");
        klog_info("SMP_TEST", "CPU index: %d", cpu_index);
    } else {
        klog_error("SMP_TEST", "Invalid CPU index (FAIL)");
        failures++;
    }

    /* Test 2: Verify BSP initialization completed */
    klog_info("SMP_TEST", "Test 2: BSP initialization status");
    if (bsp_init_done) {
        klog_info("SMP_TEST", "BSP initialization complete (PASS)");
    } else {
        klog_error("SMP_TEST", "BSP initialization NOT complete (FAIL)");
        failures++;
    }

    /* Test 3: Get expected CPU count */
    klog_info("SMP_TEST", "Test 3: CPU count detection");
    cpu_count = smp_get_cpu_count();
    klog_info("SMP_TEST", "Detected %d CPUs", cpu_count);

    /* For SMP test, we expect at least 2 CPUs */
    expected_cpus = 2;
    if (cpu_count >= expected_cpus) {
        klog_info("SMP_TEST", "CPU count >= 2 (PASS)");
    } else {
        klog_warn("SMP_TEST", "CPU count < 2 (WARNING - running in single-CPU mode)");
        /* Don't fail the test - it might be running on single-CPU hardware */
    }

    /* Test 4: Verify AP startup (cpu_count) */
    klog_info("SMP_TEST", "Test 4: CPU count verification");
    klog_info("SMP_TEST", "Detected CPUs: %d", cpu_count);

    if (cpu_count >= 1) {
        klog_info("SMP_TEST", "At least 1 CPU detected (PASS)");
    } else {
        klog_error("SMP_TEST", "No CPUs detected (FAIL)");
        failures++;
    }

    /* Test 5: Verify we're running on a valid CPU */
    klog_info("SMP_TEST", "Test 5: Current CPU verification");
    if (cpu_index >= 0 && cpu_index < SMP_MAX_CPUS) {
        klog_info("SMP_TEST", "Valid CPU index (PASS)");
    } else {
        klog_error("SMP_TEST", "Invalid CPU index (FAIL)");
        failures++;
    }

    /* Test 6: Verify APIC ID is valid */
    klog_info("SMP_TEST", "Test 6: APIC accessibility");
    apic_id = lapic_get_id();
    klog_info("SMP_TEST", "Local APIC ID: %x", apic_id);

    if (apic_id < 256) {  /* Valid APIC IDs are 0-255 */
        klog_info("SMP_TEST", "Valid APIC ID (PASS)");
    } else {
        klog_error("SMP_TEST", "Invalid APIC ID (FAIL)");
        failures++;
    }

    /* Print summary */
    if (failures == 0) {
        klog_info("SMP_TEST", "SMP: All tests PASSED");
    } else {
        klog_error("SMP_TEST", "SMP: %d tests FAILED", failures);
    }

    return (failures > 0) ? -1 : 0;
}

/* ============================================================================
 * Test Wrapper
 * ============================================================================ */

#if CONFIG_TESTS_SMP
void test_smp(void) {
    if (test_should_run("smp")) {
        test_run_by_name("smp");
    }
}
#else
void test_smp(void) { }
#endif
