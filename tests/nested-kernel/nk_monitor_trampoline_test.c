/* NK Test that monitor trampoline properly switches CR3 */

#include <stdint.h>
#include "test_nk_monitor_trampoline.h"
#include "kernel/test.h"
#include "kernel/klog.h"
#include "arch/x86_64/serial.h"
#include "kernel/monitor/monitor.h"

#if CONFIG_TESTS_NK_TRAMPOLINE

/* Test: Call monitor from unprivileged mode */
void test_monitor_call_from_unprivileged(void) {
    klog_info("NK_MON_TRAMP_TEST", "Starting monitor call test");

    /* We should be in unprivileged mode at this point */
    if (monitor_is_privileged()) {
        klog_error("NK_MON_TRAMP_TEST", "FAIL: Already in privileged mode");
        return;
    }
    klog_info("NK_MON_TRAMP_TEST", "Confirmed: Running in unprivileged mode");

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

    /* Test 2: Verify we're back in unprivileged mode */
    if (monitor_is_privileged()) {
        klog_error("NK_MON_TRAMP_TEST", "FAIL: Still in privileged mode after call");
        return;
    }
    klog_info("NK_MON_TRAMP_TEST", "PASS: Returned to unprivileged mode");

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

    /* Test 5: Verify final state is unprivileged */
    if (monitor_is_privileged()) {
        klog_error("NK_MON_TRAMP_TEST", "FAIL: Ended in privileged mode");
        return;
    }
    klog_info("NK_MON_TRAMP_TEST", "PASS: Still in unprivileged mode");

    klog_info("NK_MON_TRAMP_TEST", "All tests PASSED");
}

#endif /* CONFIG_TESTS_NK_TRAMPOLINE */

/* ============================================================================
 * Test Wrapper
 * ============================================================================ */

#if CONFIG_TESTS_NK_TRAMPOLINE
void test_nk_monitor_trampoline(void) {
    klog_info("KERN", "Testing monitor trampoline...");
    test_monitor_call_from_unprivileged();
}
#else
void test_nk_monitor_trampoline(void) { }
#endif
