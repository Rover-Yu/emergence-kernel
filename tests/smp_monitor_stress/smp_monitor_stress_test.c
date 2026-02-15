/* SMP Monitor Stress Test
 * Verifies per-CPU trampoline data and GS-base setup work correctly */

#include <stdint.h>
#include <stddef.h>
#include "test_smp_monitor_stress.h"
#include "kernel/test.h"
#include "arch/x86_64/serial.h"
#include "arch/x86_64/smp.h"
#include "include/spinlock.h"
#include "include/atomic.h"

/* Test configuration */
#define STRESS_ITERATIONS 50

/* Shared test state */
static volatile int stress_test_started = 0;
static volatile int stress_test_complete = 0;
static volatile int ap_completions[SMP_MAX_CPUS];
static volatile int errors_detected = 0;
static spinlock_t stress_lock = SPIN_LOCK_UNLOCKED;

/* Flag for AP polling (exported for smp.c) */
volatile int smp_monitor_stress_test_start = 0;

#if CONFIG_TESTS_SMP_MONITOR_STRESS

/* External functions */
extern void *monitor_pmm_alloc(uint8_t order);
extern void monitor_pmm_free(void *addr, uint8_t order);

/* RDMSR helper to read IA32_GS_BASE */
static inline uint64_t rdmsr_gs_base(void) {
    uint32_t low, high;
    asm volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(0xC0000101));
    return ((uint64_t)high << 32) | low;
}

/**
 * smp_monitor_stress_ap_entry - AP entry point for stress test
 *
 * Called by APs when stress test mode is active.
 */
void smp_monitor_stress_ap_entry(void) {
    int cpu_id = smp_get_cpu_index();
    int allocations_ok = 0;
    int frees_ok = 0;

    /* Wait for test start signal */
    while (!stress_test_started) {
        asm volatile("pause");
    }

    /* Verify GS base points to our per_cpu_data */
    uint64_t gs_base = rdmsr_gs_base();
    uint64_t expected_base = (uint64_t)&per_cpu_data[cpu_id];

    if (gs_base != expected_base) {
        serial_puts("[SMP-STRESS] CPU");
        serial_putc('0' + cpu_id);
        serial_puts(" GS base mismatch! Expected 0x");
        serial_put_hex(expected_base);
        serial_puts(" got 0x");
        serial_put_hex(gs_base);
        serial_puts("\n");
        errors_detected++;
    }

    /* Verify per_cpu_data has correct cpu_index */
    if (per_cpu_data[cpu_id].cpu_index != cpu_id) {
        serial_puts("[SMP-STRESS] CPU");
        serial_putc('0' + cpu_id);
        serial_puts(" cpu_index mismatch!\n");
        errors_detected++;
    }

    /* Stress test: multiple allocations/frees */
    void *pages[STRESS_ITERATIONS];

    for (int i = 0; i < STRESS_ITERATIONS; i++) {
        pages[i] = monitor_pmm_alloc(0);
        if (pages[i] != NULL) {
            allocations_ok++;
        }
    }

    for (int i = 0; i < STRESS_ITERATIONS; i++) {
        if (pages[i] != NULL) {
            monitor_pmm_free(pages[i], 0);
            frees_ok++;
        }
    }

    /* Report results */
    irq_flags_t flags;
    flags = spin_lock_irqsave(&stress_lock);
    serial_puts("[SMP-STRESS] CPU");
    serial_putc('0' + cpu_id);
    serial_puts(" complete: ");
    serial_put_hex(allocations_ok);
    serial_puts("/");
    serial_put_hex(STRESS_ITERATIONS);
    serial_puts(" allocs, ");
    serial_put_hex(frees_ok);
    serial_puts("/");
    serial_put_hex(STRESS_ITERATIONS);
    serial_puts(" frees\n");
    spin_unlock_irqrestore(&stress_lock, flags);

    ap_completions[cpu_id] = 1;
    smp_mb();

    /* Wait for test to complete */
    while (!stress_test_complete) {
        asm volatile("pause");
    }
}

/**
 * run_smp_monitor_stress_tests - Main test entry point
 *
 * Returns: 0 on success, -1 on failure
 */
int run_smp_monitor_stress_tests(void) {
    int cpu_count;
    int cpu_id;
    int timeout;
    int all_done;

    serial_puts("\n========================================\n");
    serial_puts("  SMP Monitor Stress Test\n");
    serial_puts("========================================\n\n");

    cpu_count = smp_get_cpu_count();
    cpu_id = smp_get_cpu_index();

    if (cpu_count < 2) {
        serial_puts("[SMP-STRESS] SKIP: Requires at least 2 CPUs\n");
        serial_puts("[SMP-STRESS] Current CPU count: ");
        serial_putc('0' + cpu_count);
        serial_puts("\n");
        return 0;
    }

    serial_puts("[SMP-STRESS] Testing with ");
    serial_putc('0' + cpu_count);
    serial_puts(" CPUs\n");

    /* Verify BSP GS base */
    uint64_t gs_base = rdmsr_gs_base();
    uint64_t expected_base = (uint64_t)&per_cpu_data[cpu_id];
    serial_puts("[SMP-STRESS] BSP (CPU0) GS base: 0x");
    serial_put_hex(gs_base);
    serial_puts("\n[SMP-STRESS] Expected: 0x");
    serial_put_hex(expected_base);
    serial_puts("\n");

    if (gs_base != expected_base) {
        serial_puts("[SMP-STRESS] BSP GS base mismatch (FAIL)\n");
        errors_detected++;
    } else {
        serial_puts("[SMP-STRESS] BSP GS base correct (PASS)\n");
    }

    /* Verify per_cpu_data initialization */
    serial_puts("[SMP-STRESS] Verifying per_cpu_data initialization:\n");
    for (int i = 0; i < cpu_count; i++) {
        serial_puts("[SMP-STRESS]   per_cpu_data[");
        serial_putc('0' + i);
        serial_puts("].cpu_index = ");
        serial_putc('0' + per_cpu_data[i].cpu_index);
        serial_puts("\n");

        if (per_cpu_data[i].cpu_index != i) {
            serial_puts("[SMP-STRESS]   ERROR: cpu_index mismatch!\n");
            errors_detected++;
        }
    }

    /* Initialize test state */
    for (int i = 0; i < SMP_MAX_CPUS; i++) {
        ap_completions[i] = 0;
    }

    /* Signal APs to start */
    stress_test_started = 1;
    smp_monitor_stress_test_start = 1;
    smp_mb();

    /* BSP also participates */
    smp_monitor_stress_ap_entry();

    /* Wait for all APs to complete */
    timeout = 10000000;
    all_done = 0;
    while (timeout-- > 0 && !all_done) {
        all_done = 1;
        for (int i = 1; i < cpu_count; i++) {
            if (!ap_completions[i]) {
                all_done = 0;
                break;
            }
        }
        asm volatile("pause");
    }

    if (!all_done) {
        serial_puts("[SMP-STRESS] TIMEOUT waiting for APs\n");
        for (int i = 1; i < cpu_count; i++) {
            if (!ap_completions[i]) {
                serial_puts("[SMP-STRESS]   CPU");
                serial_putc('0' + i);
                serial_puts(" did not complete\n");
            }
        }
        errors_detected++;
    }

    /* Signal test complete */
    stress_test_complete = 1;
    smp_mb();

    /* Small delay for APs to finish */
    for (volatile int i = 0; i < 100000; i++) {
        asm volatile("pause");
    }

    /* Report results */
    serial_puts("\n========================================\n");
    if (errors_detected == 0) {
        serial_puts("  SMP-STRESS: ALL PASSED\n");
    } else {
        serial_puts("  SMP-STRESS: FAILED (");
        serial_put_hex(errors_detected);
        serial_puts(" errors)\n");
    }
    serial_puts("========================================\n");

    return (errors_detected == 0) ? 0 : -1;
}

#endif /* CONFIG_TESTS_SMP_MONITOR_STRESS */

/* ============================================================================
 * Test Wrappers
 * ============================================================================ */

#if CONFIG_TESTS_SMP_MONITOR_STRESS
void test_smp_monitor_stress(void) {
    if (test_should_run("smp_monitor_stress")) {
        test_run_by_name("smp_monitor_stress");
    }
}
/* Note: smp_monitor_stress_ap_entry is already defined above inside CONFIG_TESTS_SMP_MONITOR_STRESS block */
#else
void test_smp_monitor_stress(void) { }

/* Provide empty stub for AP entry when stress tests are disabled */
void smp_monitor_stress_ap_entry(void) { }
#endif
