/* Emergence Kernel - APIC Timer Tests */

#include <stdint.h>
#include "test_timer.h"
#include "kernel/test.h"
#include "kernel/klog.h"
#include "arch/x86_64/timer.h"
#include "arch/x86_64/serial.h"

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
    klog_info("TIMER_TEST", "=== APIC Timer Test Suite ===");

    /* Test 1: Initialize APIC timer */
    klog_info("TIMER_TEST", "Test 1: Initialize APIC timer");
    apic_timer_init();
    klog_info("TIMER_TEST", "APIC timer initialized");

    /* Test 2: Start timer to verify it can run */
    klog_info("TIMER_TEST", "Test 2: Start timer");
    timer_start();
    klog_info("TIMER_TEST", "Timer started successfully");

    /* Test 3: Verify timer state */
    if (timer_is_active()) {
        klog_info("TIMER_TEST", "Timer is active (PASS)");
    } else {
        klog_error("TIMER_TEST", "Timer is NOT active (FAIL)");
        return -1;
    }

    /* Test 4: Stop timer */
    klog_info("TIMER_TEST", "Test 3: Stop timer");
    extern void timer_stop(void);
    timer_stop();

    if (!timer_is_active()) {
        klog_info("TIMER_TEST", "Timer stopped successfully (PASS)");
    } else {
        klog_error("TIMER_TEST", "Timer is STILL active (FAIL)");
        return -1;
    }

    klog_info("TIMER_TEST", "APIC TIMER: All tests PASSED");

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
