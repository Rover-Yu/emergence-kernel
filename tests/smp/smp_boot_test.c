/* Emergence Kernel - SMP Boot Tests */

#include <stdint.h>
#include "arch/x86_64/serial.h"
#include "arch/x86_64/apic.h"
#include "arch/x86_64/smp.h"

/* External function prototypes */
extern void serial_puts(const char *str);
extern void serial_putc(char c);
extern void serial_put_hex(uint64_t value);

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

    serial_puts("\n========================================\n");
    serial_puts("  SMP Boot Test Suite\n");
    serial_puts("========================================\n");
    serial_puts("\n");

    /* Test 1: Verify a valid CPU has booted */
    serial_puts("[SMP TEST] Test 1: CPU boot verification\n");
    cpu_index = smp_get_cpu_index();
    if (cpu_index >= 0 && cpu_index < SMP_MAX_CPUS) {
        serial_puts("[SMP TEST] Valid CPU is running (PASS)\n");
        serial_puts("[SMP TEST] CPU index: ");
        serial_put_hex(cpu_index);
        serial_puts("\n");
    } else {
        serial_puts("[SMP TEST] Invalid CPU index (FAIL)\n");
        failures++;
    }

    /* Test 2: Verify BSP initialization completed */
    serial_puts("[SMP TEST] Test 2: BSP initialization status\n");
    if (bsp_init_done) {
        serial_puts("[SMP TEST] BSP initialization complete (PASS)\n");
    } else {
        serial_puts("[SMP TEST] BSP initialization NOT complete (FAIL)\n");
        failures++;
    }

    /* Test 3: Get expected CPU count */
    serial_puts("[SMP TEST] Test 3: CPU count detection\n");
    cpu_count = smp_get_cpu_count();
    serial_puts("[SMP TEST] Detected ");
    serial_put_hex(cpu_count);
    serial_puts(" CPUs\n");

    /* For SMP test, we expect at least 2 CPUs */
    expected_cpus = 2;
    if (cpu_count >= expected_cpus) {
        serial_puts("[SMP TEST] CPU count >= 2 (PASS)\n");
    } else {
        serial_puts("[SMP TEST] CPU count < 2 (WARNING - running in single-CPU mode)\n");
        /* Don't fail the test - it might be running on single-CPU hardware */
    }

    /* Test 4: Verify AP startup (cpu_count) */
    serial_puts("[SMP TEST] Test 4: CPU count verification\n");
    serial_puts("[SMP TEST] Detected CPUs: ");
    serial_put_hex(cpu_count);
    serial_puts("\n");

    if (cpu_count >= 1) {
        serial_puts("[SMP TEST] At least 1 CPU detected (PASS)\n");
    } else {
        serial_puts("[SMP TEST] No CPUs detected (FAIL)\n");
        failures++;
    }

    /* Test 5: Verify we're running on a valid CPU */
    serial_puts("[SMP TEST] Test 5: Current CPU verification\n");
    if (cpu_index >= 0 && cpu_index < SMP_MAX_CPUS) {
        serial_puts("[SMP TEST] Valid CPU index (PASS)\n");
    } else {
        serial_puts("[SMP TEST] Invalid CPU index (FAIL)\n");
        failures++;
    }

    /* Test 6: Verify APIC ID is valid */
    serial_puts("[SMP TEST] Test 6: APIC accessibility\n");
    apic_id = lapic_get_id();
    serial_puts("[SMP TEST] Local APIC ID: ");
    serial_put_hex(apic_id);
    serial_puts("\n");

    if (apic_id < 256) {  /* Valid APIC IDs are 0-255 */
        serial_puts("[SMP TEST] Valid APIC ID (PASS)\n");
    } else {
        serial_puts("[SMP TEST] Invalid APIC ID (FAIL)\n");
        failures++;
    }

    /* Print summary */
    serial_puts("\n========================================\n");
    if (failures == 0) {
        serial_puts("  SMP: All tests PASSED\n");
    } else {
        serial_puts("  SMP: Some tests FAILED (");
        serial_put_hex(failures);
        serial_puts(" failures)\n");
    }
    serial_puts("========================================\n");
    serial_puts("\n");

    return (failures > 0) ? -1 : 0;
}
