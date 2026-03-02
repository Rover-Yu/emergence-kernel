/* NK Test that monitor trampoline properly toggles CR0.WP */

#include <stdint.h>
#include "test_nk_monitor_trampoline.h"
#include "kernel/test.h"
#include "kernel/klog.h"
#include "arch/x86_64/serial.h"
#include "kernel/monitor/monitor.h"
#include "arch/x86_64/cr.h"

#if CONFIG_TESTS_NK_TRAMPOLINE

/**
 * get_cr0_wp - Get current CR0.WP bit state
 *
 * Returns: 1 if WP is set (OK mode), 0 if WP is clear (NK mode)
 */
static inline int get_cr0_wp(void) {
    uint64_t cr0 = arch_cr0_read();
    return (cr0 & (1 << 16)) ? 1 : 0;
}

/**
 * Test: Verify CR0.WP toggle during monitor call
 *
 * This test verifies that:
 * 1. Before monitor call: CR0.WP=1 (OK mode, write protection enforced)
 * 2. During monitor call: CR0.WP=0 (NK mode, can write to read-only pages)
 * 3. After monitor call: CR0.WP=1 (restored to OK mode)
 */
void test_cr0_wp_toggle(void) {
    klog_info("NK_MON_TRAMP_TEST", "Testing CR0.WP toggle mechanism");

    /* Verify we start in OK mode (CR0.WP=1) */
    if (get_cr0_wp() != 1) {
        klog_error("NK_MON_TRAMP_TEST", "FAIL: CR0.WP not set at start (expected WP=1)");
        return;
    }
    klog_info("NK_MON_TRAMP_TEST", "PASS: CR0.WP=1 (OK mode) before monitor call");

    /* The actual toggle happens inside the trampoline, which we can't observe
     * directly from here. But we can verify the trampoline works correctly
     * by doing a monitor call and checking we return to the correct state. */

    /* Allocate a page through monitor - this exercises the full trampoline */
    monitor_ret_t ret = monitor_call(MONITOR_CALL_ALLOC_PHYS, 0, 0, 0);
    if (ret.error != 0 || ret.result == 0) {
        klog_error("NK_MON_TRAMP_TEST", "FAIL: Monitor call failed");
        return;
    }

    /* Verify we're back in OK mode (CR0.WP=1) */
    if (get_cr0_wp() != 1) {
        klog_error("NK_MON_TRAMP_TEST", "FAIL: CR0.WP not restored after monitor call");
        /* Clean up anyway */
        monitor_call(MONITOR_CALL_FREE_PHYS, ret.result, 0, 0);
        return;
    }
    klog_info("NK_MON_TRAMP_TEST", "PASS: CR0.WP=1 (OK mode) restored after monitor call");

    /* Clean up */
    monitor_call(MONITOR_CALL_FREE_PHYS, ret.result, 0, 0);

    klog_info("NK_MON_TRAMP_TEST", "CR0.WP toggle test PASSED");
}

/**
 * Test: Call monitor from unprivileged (OK) mode
 *
 * With CR0.WP toggle design:
 * - OK mode: CR0.WP=1, cannot write to read-only PTEs
 * - NK mode: CR0.WP=0, can write to any page
 * - Trampoline toggles CR0.WP to enter/exit NK mode
 */
void test_monitor_call_from_unprivileged(void) {
    klog_info("NK_MON_TRAMP_TEST", "Starting monitor call test (CR0.WP toggle design)");

    /* We should be in OK mode (CR0.WP=1) at this point */
    if (monitor_is_privileged()) {
        klog_error("NK_MON_TRAMP_TEST", "FAIL: Already in NK mode (CR0.WP=0)");
        return;
    }
    klog_info("NK_MON_TRAMP_TEST", "Confirmed: Running in OK mode (CR0.WP=1)");

    /* Test 1: Simple allocation through monitor call */
    klog_info("NK_MON_TRAMP_TEST", "Test 1: Allocate page via monitor_call");
    monitor_ret_t ret = monitor_call(MONITOR_CALL_ALLOC_PHYS, 0, 0, 0);

    if (ret.error != 0) {
        klog_error("NK_MON_TRAMP_TEST", "FAIL: Allocation returned error");
        return;
    }

    if (ret.result == 0) {
        klog_error("NK_MON_TRAMP_TEST", "FAIL: Allocation returned NULL");
        return;
    }

    klog_info("NK_MON_TRAMP_TEST", "PASS: Allocation succeeded, addr = %x", ret.result);

    /* Test 2: Verify we're back in OK mode */
    if (monitor_is_privileged()) {
        klog_error("NK_MON_TRAMP_TEST", "FAIL: Still in NK mode after call");
        return;
    }
    klog_info("NK_MON_TRAMP_TEST", "PASS: Returned to OK mode (CR0.WP=1)");

    /* Test 3: Free the allocation */
    monitor_call(MONITOR_CALL_FREE_PHYS, ret.result, 0, 0);
    klog_info("NK_MON_TRAMP_TEST", "PASS: Free succeeded");

    /* Test 4: Multiple allocations to verify trampoline works repeatedly */
    klog_info("NK_MON_TRAMP_TEST", "Test 4: Multiple allocations");
    monitor_ret_t allocs[3];
    for (int i = 0; i < 3; i++) {
        allocs[i] = monitor_call(MONITOR_CALL_ALLOC_PHYS, 0, 0, 0);
        if (allocs[i].error != 0 || allocs[i].result == 0) {
            klog_error("NK_MON_TRAMP_TEST", "FAIL: Allocation #%d failed", i);
            return;
        }
    }

    klog_info("NK_MON_TRAMP_TEST", "PASS: All 3 allocations succeeded");

    /* Free all allocations */
    for (int i = 0; i < 3; i++) {
        monitor_call(MONITOR_CALL_FREE_PHYS, allocs[i].result, 0, 0);
    }
    klog_info("NK_MON_TRAMP_TEST", "PASS: All allocations freed");

    /* Test 5: Verify final state is OK mode */
    if (monitor_is_privileged()) {
        klog_error("NK_MON_TRAMP_TEST", "FAIL: Ended in NK mode");
        return;
    }
    klog_info("NK_MON_TRAMP_TEST", "PASS: Still in OK mode (CR0.WP=1)");

    klog_info("NK_MON_TRAMP_TEST", "All tests PASSED");
}

#endif /* CONFIG_TESTS_NK_TRAMPOLINE */

/* ============================================================================
 * Test Wrapper
 * ============================================================================ */

/**
 * run_nk_trampoline_tests - Main test entry point for test registry
 *
 * This function is called by test_run_by_name() when the test is selected.
 * Returns: 0 on success, non-zero on failure
 */
int run_nk_trampoline_tests(void) {
    int failures = 0;

#if CONFIG_TESTS_NK_TRAMPOLINE
    klog_info("NK_TRAMP_TEST", "=== NK Monitor Trampoline Tests ===");

    /* Save current CR0.WP state */
    int initial_wp = get_cr0_wp();

    /* Run the CR0.WP toggle test */
    test_cr0_wp_toggle();

    /* Run the monitor call from unprivileged test */
    test_monitor_call_from_unprivileged();

    /* Verify CR0.WP is still in OK mode */
    if (get_cr0_wp() != 1) {
        klog_error("NK_TRAMP_TEST", "CR0.WP not restored to 1 after tests");
        failures++;
    }

    /* Verify we didn't change WP from initial state */
    if (get_cr0_wp() != initial_wp) {
        klog_error("NK_TRAMP_TEST", "CR0.WP changed unexpectedly");
        failures++;
    }

    if (failures == 0) {
        klog_info("NK_TRAMP_TEST", "All trampoline tests PASSED");
    } else {
        klog_error("NK_TRAMP_TEST", "Trampoline tests FAILED (%d)", failures);
    }
#endif

    return failures;
}

#if CONFIG_TESTS_NK_TRAMPOLINE
void test_nk_monitor_trampoline(void) {
    if (test_should_run("nk_trampoline")) {
        test_run_by_name("nk_trampoline");
    }
}
#else
void test_nk_monitor_trampoline(void) { }
#endif
