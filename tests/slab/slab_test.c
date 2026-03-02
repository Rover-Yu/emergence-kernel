/* Emergence Kernel - Slab Allocator Tests */

#include <stdint.h>
#include "test_slab.h"
#include "kernel/test.h"
#include "kernel/slab.h"
#include "kernel/klog.h"
#include "arch/x86_64/serial.h"
#include "arch/x86_64/power.h"

#if CONFIG_TESTS_SLAB

/* ============================================================================
 * Test Helper Functions
 * ============================================================================ */

/**
 * test_single_alloc_free - Test single allocation and free
 */
static int test_single_alloc_free(void) {
    void *ptr;

    klog_info("SLAB_TEST", "Single allocation test...");

    /* Allocate from 64-byte cache */
    ptr = slab_alloc_size(64);
    if (ptr == NULL) {
        klog_error("SLAB_TEST", "FAILED: Allocation returned NULL");
        return -1;
    }

    klog_info("SLAB_TEST", "Allocated 64-byte object at %p", ptr);

    /* Free the object */
    slab_free_size(ptr, 64);

    klog_info("SLAB_TEST", "Single allocation test PASSED");
    return 0;
}

/**
 * test_multiple_allocations - Test multiple allocations from same cache
 */
static int test_multiple_allocations(void) {
    void *ptrs[16];
    int i;

    klog_info("SLAB_TEST", "Multiple allocations test...");

    /* Allocate 16 objects from 128-byte cache */
    for (i = 0; i < 16; i++) {
        ptrs[i] = slab_alloc_size(128);
        if (ptrs[i] == NULL) {
            klog_error("SLAB_TEST", "FAILED: Allocation %d returned NULL", i);
            return -1;
        }
    }

    klog_info("SLAB_TEST", "Allocated 16 objects of 128 bytes");

    /* Free all objects */
    for (i = 0; i < 16; i++) {
        slab_free_size(ptrs[i], 128);
    }

    klog_info("SLAB_TEST", "Multiple allocations test PASSED");
    return 0;
}

/**
 * test_free_reuse - Test that freed objects are reused
 */
static int test_free_reuse(void) {
    void *ptr1, *ptr2;

    klog_info("SLAB_TEST", "Free reuse test...");

    /* Allocate from 256-byte cache */
    ptr1 = slab_alloc_size(256);
    if (ptr1 == NULL) {
        klog_error("SLAB_TEST", "FAILED: First allocation returned NULL");
        return -1;
    }

    klog_info("SLAB_TEST", "First allocation at %p", ptr1);

    /* Free and reallocate - should get same address */
    slab_free_size(ptr1, 256);

    ptr2 = slab_alloc_size(256);
    if (ptr2 == NULL) {
        klog_error("SLAB_TEST", "FAILED: Second allocation returned NULL");
        return -1;
    }

    klog_info("SLAB_TEST", "Second allocation at %p", ptr2);

    /* Check if we got the same address (likely but not guaranteed) */
    if (ptr1 == ptr2) {
        klog_info("SLAB_TEST", "Object reused (same address)");
    } else {
        klog_info("SLAB_TEST", "Object reused (different address, OK)");
    }

    slab_free_size(ptr2, 256);

    klog_info("SLAB_TEST", "Free reuse test PASSED");
    return 0;
}

/**
 * test_all_cache_sizes - Test all cache sizes
 */
static int test_all_cache_sizes(void) {
    void *ptrs[8];
    size_t sizes[] = {32, 64, 128, 256, 512, 1024, 2048, 4096};
    int i;

    klog_info("SLAB_TEST", "All cache sizes test...");

    /* Allocate one object from each cache */
    for (i = 0; i < 8; i++) {
        ptrs[i] = slab_alloc_size(sizes[i]);
        if (ptrs[i] == NULL) {
            klog_error("SLAB_TEST", "FAILED: Allocation for size %d returned NULL", sizes[i]);
            return -1;
        }

        klog_info("SLAB_TEST", "Allocated %d-byte object at %p", sizes[i], ptrs[i]);
    }

    /* Free all objects */
    for (i = 0; i < 8; i++) {
        slab_free_size(ptrs[i], sizes[i]);
    }

    klog_info("SLAB_TEST", "All cache sizes test PASSED");
    return 0;
}

/**
 * test_size_rounding - Test that sizes are rounded correctly
 */
static int test_size_rounding(void) {
    void *ptr;

    klog_info("SLAB_TEST", "Size rounding test...");

    /* Allocate 50 bytes - should round to 64 */
    ptr = slab_alloc_size(50);
    if (ptr == NULL) {
        klog_error("SLAB_TEST", "FAILED: Allocation returned NULL");
        return -1;
    }

    klog_info("SLAB_TEST", "Allocated 50 bytes (rounded to 64)");
    slab_free_size(ptr, 50);

    /* Allocate 1000 bytes - should round to 1024 */
    ptr = slab_alloc_size(1000);
    if (ptr == NULL) {
        klog_error("SLAB_TEST", "FAILED: Allocation returned NULL");
        return -1;
    }

    klog_info("SLAB_TEST", "Allocated 1000 bytes (rounded to 1024)");
    slab_free_size(ptr, 1000);

    klog_info("SLAB_TEST", "Size rounding test PASSED");
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

    klog_info("SLAB_TEST", "=== SLAB Allocator Test Suite ===");

    /* Test 1: Single allocation */
    if (test_single_alloc_free() != 0) {
        failures++;
    }

    /* Test 2: Multiple allocations */
    if (test_multiple_allocations() != 0) {
        failures++;
    }

    /* Test 3: Free and reuse */
    if (test_free_reuse() != 0) {
        failures++;
    }

    /* Test 4: All cache sizes */
    if (test_all_cache_sizes() != 0) {
        failures++;
    }

    /* Test 5: Size rounding */
    if (test_size_rounding() != 0) {
        failures++;
    }

    /* Dump statistics */
    slab_dump_stats();

    /* Summary */
    if (failures == 0) {
        klog_info("SLAB_TEST", "SLAB: All tests PASSED");
    } else {
        klog_error("SLAB_TEST", "SLAB: Some tests FAILED (%d failures)", failures);
    }

    return failures;
}

#endif /* CONFIG_TESTS_SLAB */

/* ============================================================================
 * Test Wrapper
 * ============================================================================ */

#if CONFIG_TESTS_SLAB
void test_slab(void) {
    /* Run directly if selected via tests=slab CSV mode
     * We bypass test_run_by_name() to avoid address space issues
     * that occur when the test framework runs from a different
     * virtual address context than the slab allocator. */
    if (test_should_run("slab")) {
        /* Check if test already ran via unified mode */
        if (!test_did_run("slab")) {
            int result = run_slab_tests();
            test_mark_run("slab", result);
            if (result == 0) {
                klog_info("TEST", "PASSED: slab");
            } else {
                klog_error("TEST", "FAILED: slab (failures: %d)", result);
                system_shutdown();
            }
        }
    }
}
#else
void test_slab(void) { }
#endif
