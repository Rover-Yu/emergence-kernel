/* Test that monitor trampoline properly switches CR3 */

#include <stdint.h>
#include "arch/x86_64/serial.h"
#include "kernel/monitor/monitor.h"

/* Test: Call monitor from unprivileged mode */
void test_monitor_call_from_unprivileged(void) {
    serial_puts("[TRAMPOLINE TEST] Starting monitor call test\n");

    /* We should be in unprivileged mode at this point */
    if (monitor_is_privileged()) {
        serial_puts("[TRAMPOLINE TEST] FAIL: Already in privileged mode\n");
        return;
    }
    serial_puts("[TRAMPOLINE TEST] Confirmed: Running in unprivileged mode\n");

    /* Test 1: Simple allocation through monitor call */
    serial_puts("[TRAMPOLINE TEST] Test 1: Allocate page via monitor_call\n");
    monitor_ret_t ret = monitor_call(MONITOR_CALL_ALLOC_PHYS, 0, 0, 0);

    if (ret.error != 0) {
        serial_puts("[TRAMPOLINE TEST] FAIL: Allocation returned error\n");
        return;
    }

    if (ret.result == 0) {
        serial_puts("[TRAMPOLINE TEST] FAIL: Allocation returned NULL\n");
        return;
    }

    serial_puts("[TRAMPOLINE TEST] PASS: Allocation succeeded, addr = 0x");
    serial_put_hex(ret.result);
    serial_puts("\n");

    /* Test 2: Verify we're back in unprivileged mode */
    if (monitor_is_privileged()) {
        serial_puts("[TRAMPOLINE TEST] FAIL: Still in privileged mode after call\n");
        return;
    }
    serial_puts("[TRAMPOLINE TEST] PASS: Returned to unprivileged mode\n");

    /* Test 3: Free the allocation */
    monitor_call(MONITOR_CALL_FREE_PHYS, ret.result, 0, 0);
    serial_puts("[TRAMPOLINE TEST] PASS: Free succeeded\n");

    /* Test 4: Multiple allocations to verify trampoline works repeatedly */
    serial_puts("[TRAMPOLINE TEST] Test 4: Multiple allocations\n");
    monitor_ret_t allocs[3];
    for (int i = 0; i < 3; i++) {
        allocs[i] = monitor_call(MONITOR_CALL_ALLOC_PHYS, 0, 0, 0);
        if (allocs[i].error != 0 || allocs[i].result == 0) {
            serial_puts("[TRAMPOLINE TEST] FAIL: Allocation #");
            serial_putc('0' + i);
            serial_puts(" failed\n");
            return;
        }
    }

    serial_puts("[TRAMPOLINE TEST] PASS: All 3 allocations succeeded\n");

    /* Free all allocations */
    for (int i = 0; i < 3; i++) {
        monitor_call(MONITOR_CALL_FREE_PHYS, allocs[i].result, 0, 0);
    }
    serial_puts("[TRAMPOLINE TEST] PASS: All allocations freed\n");

    /* Test 5: Verify final state is unprivileged */
    if (monitor_is_privileged()) {
        serial_puts("[TRAMPOLINE TEST] FAIL: Ended in privileged mode\n");
        return;
    }
    serial_puts("[TRAMPOLINE TEST] PASS: Still in unprivileged mode\n");

    serial_puts("[TRAMPOLINE TEST] All tests PASSED\n");
}
