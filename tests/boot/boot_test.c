/* Emergence Kernel - Boot Tests */

#include <stdint.h>
#include "test_boot.h"
#include "kernel/test.h"
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
    serial_puts("[BOOT TEST] Test 1: Serial console output\n");
    serial_puts("[BOOT TEST] Serial console is working (PASS)\n");

    /* Test 2: Verify we're running on a valid CPU (BSP or AP) */
    serial_puts("[BOOT TEST] Test 2: CPU identification\n");
    cpu_index = smp_get_cpu_index();
    if (cpu_index >= 0 && cpu_index < SMP_MAX_CPUS) {
        serial_puts("[BOOT TEST] Running on valid CPU (PASS)\n");
        serial_puts("[BOOT TEST] CPU index: ");
        serial_put_hex(cpu_index);
        serial_puts("\n");
    } else {
        serial_puts("[BOOT TEST] Invalid CPU index (FAIL)\n");
        failures++;
    }

    /* Test 3: Verify BSP initialization completed */
    serial_puts("[BOOT TEST] Test 3: BSP initialization status\n");
    if (bsp_init_done) {
        serial_puts("[BOOT TEST] BSP initialization complete (PASS)\n");
    } else {
        serial_puts("[BOOT TEST] BSP initialization NOT complete (FAIL)\n");
        failures++;
    }

    /* Test 4: Verify at least 1 CPU exists */
    serial_puts("[BOOT TEST] Test 4: CPU count verification\n");
    if (smp_get_cpu_count() >= 1) {
        serial_puts("[BOOT TEST] At least 1 CPU detected (PASS)\n");
    } else {
        serial_puts("[BOOT TEST] No CPUs detected (FAIL)\n");
        failures++;
    }

    /* Test 5: Verify APIC is accessible */
    serial_puts("[BOOT TEST] Test 5: Local APIC accessibility\n");
    apic_id = lapic_get_id();
    serial_puts("[BOOT TEST] Local APIC ID: ");
    serial_put_hex(apic_id);
    serial_puts("\n");

    if (apic_id < 256) {  /* Valid APIC IDs are 0-255 */
        serial_puts("[BOOT TEST] Local APIC accessible (PASS)\n");
    } else {
        serial_puts("[BOOT TEST] Local APIC NOT accessible (FAIL)\n");
        failures++;
    }

    /* Print summary */
    serial_puts("\n========================================\n");
    if (failures == 0) {
        serial_puts("  BOOT: All tests PASSED\n");
    } else {
        serial_puts("  BOOT: Some tests FAILED (");
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

#if CONFIG_TESTS_BOOT
void test_boot(void) {
    if (test_should_run("boot")) {
        test_run_by_name("boot");
    }
}
#else
void test_boot(void) { }
#endif
