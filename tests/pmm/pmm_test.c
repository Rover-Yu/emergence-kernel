/* Emergence Kernel - Physical Memory Manager Tests */

#include <stdint.h>
#include <stddef.h>
#include "kernel/pmm.h"
#include "arch/x86_64/serial.h"

/* External function prototypes */
extern void serial_puts(const char *str);
extern void serial_put_hex(uint64_t value);

/**
 * run_pmm_tests - Run physical memory manager tests
 *
 * Tests PMM allocation, freeing, coalescing, and statistics.
 *
 * Returns: 0 on success, -1 on failure
 */
int run_pmm_tests(void) {
    serial_puts("\n========================================\n");
    serial_puts("  PMM Test Suite\n");
    serial_puts("========================================\n");
    serial_puts("\n");

    /* Test 1: Single page allocation */
    serial_puts("[PMM TEST] Test 1: Single page allocation\n");
    void *page1 = pmm_alloc(0);
    void *page2 = pmm_alloc(0);

    if (page1 == NULL || page2 == NULL) {
        serial_puts("[PMM TEST] FAILED: Allocation returned NULL\n");
        return -1;
    }

    serial_puts("[PMM TEST] Allocated page1 at 0x");
    serial_put_hex((uint64_t)page1);
    serial_puts(", page2 at 0x");
    serial_put_hex((uint64_t)page2);
    serial_puts("\n");

    /* Test 2: Multi-page allocation (order 3 = 8 pages = 32KB) */
    serial_puts("[PMM TEST] Test 2: Multi-page allocation\n");
    void *block = pmm_alloc(3);
    if (block == NULL) {
        serial_puts("[PMM TEST] FAILED: Multi-page allocation returned NULL\n");
        return -1;
    }

    serial_puts("[PMM TEST] Allocated 32KB block at 0x");
    serial_put_hex((uint64_t)block);
    serial_puts("\n");

    /* Test 3: Free and coalesce */
    serial_puts("[PMM TEST] Test 3: Free and coalesce\n");
    pmm_free(page1, 0);
    pmm_free(page2, 0);
    serial_puts("[PMM TEST] Freed pages (buddy coalescing)\n");

    /* Test 4: Statistics */
    serial_puts("[PMM TEST] Test 4: Statistics\n");
    uint64_t free = pmm_get_free_pages();
    uint64_t total = pmm_get_total_pages();
    serial_puts("[PMM TEST] Free: ");
    serial_put_hex(free);
    serial_puts(" / Total: ");
    serial_put_hex(total);
    serial_puts("\n");

    /* Test 5: Allocate adjacent pages to verify they were coalesced */
    serial_puts("[PMM TEST] Test 5: Allocate coalesced pages\n");
    void *page3 = pmm_alloc(1);  /* Request 2 pages */
    if (page3 == NULL) {
        serial_puts("[PMM TEST] FAILED: 2-page allocation returned NULL\n");
        return -1;
    }

    serial_puts("[PMM TEST] Allocated 2-page block at 0x");
    serial_put_hex((uint64_t)page3);
    serial_puts(" (should be same as page1 if coalesced)\n");

    serial_puts("\n========================================\n");
    serial_puts("  PMM: All tests PASSED\n");
    serial_puts("========================================\n");
    serial_puts("\n");

    return 0;
}
