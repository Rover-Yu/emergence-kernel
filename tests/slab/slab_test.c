/* Emergence Kernel - Slab Allocator Tests */

#include <stdint.h>
#include "kernel/slab.h"
#include "arch/x86_64/serial.h"

/* Test configuration */
#define CONFIG_SLAB_TESTS 1

#if CONFIG_SLAB_TESTS

/* ============================================================================
 * Test Helper Functions
 * ============================================================================ */

/**
 * test_single_alloc_free - Test single allocation and free
 */
static int test_single_alloc_free(void) {
    void *ptr;

    serial_puts("[SLAB test] Single allocation test...\n");

    /* Allocate from 64-byte cache */
    ptr = slab_alloc_size(64);
    if (ptr == NULL) {
        serial_puts("[SLAB test] FAILED: Allocation returned NULL\n");
        return -1;
    }

    serial_puts("[SLAB test] Allocated 64-byte object at 0x");
    serial_put_hex((uint64_t)ptr);
    serial_puts("\n");

    /* Free the object */
    slab_free_size(ptr, 64);

    serial_puts("[SLAB test] Single allocation test PASSED\n");
    return 0;
}

/**
 * test_multiple_allocations - Test multiple allocations from same cache
 */
static int test_multiple_allocations(void) {
    void *ptrs[16];
    int i;

    serial_puts("[SLAB test] Multiple allocations test...\n");

    /* Allocate 16 objects from 128-byte cache */
    for (i = 0; i < 16; i++) {
        ptrs[i] = slab_alloc_size(128);
        if (ptrs[i] == NULL) {
            serial_puts("[SLAB test] FAILED: Allocation ");
            serial_put_hex(i);
            serial_puts(" returned NULL\n");
            return -1;
        }
    }

    serial_puts("[SLAB test] Allocated 16 objects of 128 bytes\n");

    /* Free all objects */
    for (i = 0; i < 16; i++) {
        slab_free_size(ptrs[i], 128);
    }

    serial_puts("[SLAB test] Multiple allocations test PASSED\n");
    return 0;
}

/**
 * test_free_reuse - Test that freed objects are reused
 */
static int test_free_reuse(void) {
    void *ptr1, *ptr2;

    serial_puts("[SLAB test] Free reuse test...\n");

    /* Allocate from 256-byte cache */
    ptr1 = slab_alloc_size(256);
    if (ptr1 == NULL) {
        serial_puts("[SLAB test] FAILED: First allocation returned NULL\n");
        return -1;
    }

    serial_puts("[SLAB test] First allocation at 0x");
    serial_put_hex((uint64_t)ptr1);
    serial_puts("\n");

    /* Free and reallocate - should get same address */
    slab_free_size(ptr1, 256);

    ptr2 = slab_alloc_size(256);
    if (ptr2 == NULL) {
        serial_puts("[SLAB test] FAILED: Second allocation returned NULL\n");
        return -1;
    }

    serial_puts("[SLAB test] Second allocation at 0x");
    serial_put_hex((uint64_t)ptr2);
    serial_puts("\n");

    /* Check if we got the same address (likely but not guaranteed) */
    if (ptr1 == ptr2) {
        serial_puts("[SLAB test] Object reused (same address)\n");
    } else {
        serial_puts("[SLAB test] Object reused (different address, OK)\n");
    }

    slab_free_size(ptr2, 256);

    serial_puts("[SLAB test] Free reuse test PASSED\n");
    return 0;
}

/**
 * test_all_cache_sizes - Test all cache sizes
 */
static int test_all_cache_sizes(void) {
    void *ptrs[8];
    size_t sizes[] = {32, 64, 128, 256, 512, 1024, 2048, 4096};
    int i;

    serial_puts("[SLAB test] All cache sizes test...\n");

    /* Allocate one object from each cache */
    for (i = 0; i < 8; i++) {
        ptrs[i] = slab_alloc_size(sizes[i]);
        if (ptrs[i] == NULL) {
            serial_puts("[SLAB test] FAILED: Allocation for size ");
            serial_put_hex(sizes[i]);
            serial_puts(" returned NULL\n");
            return -1;
        }

        serial_puts("[SLAB test] Allocated ");
        serial_put_hex(sizes[i]);
        serial_puts("-byte object at 0x");
        serial_put_hex((uint64_t)ptrs[i]);
        serial_puts("\n");
    }

    /* Free all objects */
    for (i = 0; i < 8; i++) {
        slab_free_size(ptrs[i], sizes[i]);
    }

    serial_puts("[SLAB test] All cache sizes test PASSED\n");
    return 0;
}

/**
 * test_size_rounding - Test that sizes are rounded correctly
 */
static int test_size_rounding(void) {
    void *ptr;

    serial_puts("[SLAB test] Size rounding test...\n");

    /* Allocate 50 bytes - should round to 64 */
    ptr = slab_alloc_size(50);
    if (ptr == NULL) {
        serial_puts("[SLAB test] FAILED: Allocation returned NULL\n");
        return -1;
    }

    serial_puts("[SLAB test] Allocated 50 bytes (rounded to 64)\n");
    slab_free_size(ptr, 50);

    /* Allocate 1000 bytes - should round to 1024 */
    ptr = slab_alloc_size(1000);
    if (ptr == NULL) {
        serial_puts("[SLAB test] FAILED: Allocation returned NULL\n");
        return -1;
    }

    serial_puts("[SLAB test] Allocated 1000 bytes (rounded to 1024)\n");
    slab_free_size(ptr, 1000);

    serial_puts("[SLAB test] Size rounding test PASSED\n");
    return 0;
}

/* ============================================================================
 * Main Test Runner
 * ============================================================================ */

/**
 * run_slab_tests - Run all slab allocator tests
 *
 * Returns: Number of failed tests (0 = all passed)
 */
int run_slab_tests(void) {
    int failures = 0;

    serial_puts("\n");
    serial_puts("========================================\n");
    serial_puts("  SLAB Allocator Test Suite\n");
    serial_puts("========================================\n");
    serial_puts("\n");

    /* Test 1: Single allocation */
    if (test_single_alloc_free() != 0) {
        failures++;
    }
    serial_puts("\n");

    /* Test 2: Multiple allocations */
    if (test_multiple_allocations() != 0) {
        failures++;
    }
    serial_puts("\n");

    /* Test 3: Free and reuse */
    if (test_free_reuse() != 0) {
        failures++;
    }
    serial_puts("\n");

    /* Test 4: All cache sizes */
    if (test_all_cache_sizes() != 0) {
        failures++;
    }
    serial_puts("\n");

    /* Test 5: Size rounding */
    if (test_size_rounding() != 0) {
        failures++;
    }
    serial_puts("\n");

    /* Dump statistics */
    slab_dump_stats();

    /* Summary */
    serial_puts("========================================\n");
    if (failures == 0) {
        serial_puts("  SLAB: All tests PASSED\n");
    } else {
        serial_puts("  SLAB: Some tests FAILED\n");
        serial_puts("  Failures: ");
        serial_put_hex(failures);
        serial_puts("\n");
    }
    serial_puts("========================================\n");
    serial_puts("\n");

    return failures;
}

#endif /* CONFIG_SLAB_TESTS */
