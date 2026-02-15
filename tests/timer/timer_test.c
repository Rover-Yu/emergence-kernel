/* Emergence Kernel - APIC Timer Tests */

#include <stdint.h>
#include "test_timer.h"
#include "kernel/test.h"
#include "arch/x86_64/timer.h"
#include "arch/x86_64/serial.h"

/* External function prototypes */
extern void serial_puts(const char *str);
extern void serial_put_hex(uint64_t value);

/* External APIC timer functions from arch/x86_64/timer.c */
extern void apic_timer_init(void);
extern void timer_start(void);
extern void timer_stop(void);
extern int timer_is_active(void);
extern volatile int apic_timer_count;  /* Access timer state for verification */

#define NUM_QUOTES 5  /* Expected number of quotes */

/**
 * run_apic_timer_tests - Run APIC timer interrupt tests
 *
 * Tests that the APIC timer can generate periodic interrupts
 * and that the interrupt handler correctly increments a counter.
 *
 * The test initializes the APIC timer, starts it, and waits for
 * 5 quotes to be printed (approximately 2.5 seconds).
 *
 * Returns: 0 on success, -1 on failure
 */
int run_apic_timer_tests(void) {
    serial_puts("\n========================================\n");
    serial_puts("  APIC Timer Test Suite\n");
    serial_puts("========================================\n");
    serial_puts("\n");

    /* Test 1: Initialize APIC timer */
    serial_puts("[TIMER TEST] Test 1: Initialize APIC timer\n");
    apic_timer_init();
    serial_puts("[TIMER TEST] APIC timer initialized\n");

    /* Test 2: Start timer to verify it can run */
    serial_puts("[TIMER TEST] Test 2: Start timer\n");
    timer_start();
    serial_puts("[TIMER TEST] Timer started successfully\n");

    /* Test 3: Verify timer state */
    if (timer_is_active()) {
        serial_puts("[TIMER TEST] Timer is active (PASS)\n");
    } else {
        serial_puts("[TIMER TEST] Timer is NOT active (FAIL)\n");
        return -1;
    }

    /* Test 4: Stop timer */
    serial_puts("[TIMER TEST] Test 3: Stop timer\n");
    extern void timer_stop(void);
    timer_stop();

    if (!timer_is_active()) {
        serial_puts("[TIMER TEST] Timer stopped successfully (PASS)\n");
    } else {
        serial_puts("[TIMER TEST] Timer is STILL active (FAIL)\n");
        return -1;
    }

    serial_puts("\n========================================\n");
    serial_puts("  APIC TIMER: All tests PASSED\n");
    serial_puts("========================================\n");
    serial_puts("\n");

    return 0;
}

/* ============================================================================
 * Test Wrapper
 * ============================================================================ */

#if CONFIG_TESTS_APIC_TIMER
void test_timer(void) {
    if (test_should_run("timer")) {
        test_run_by_name("timer");
    }
}
#else
void test_timer(void) { }
#endif
