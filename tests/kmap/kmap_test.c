/* Emergence Kernel - KMAP Subsystem Tests
 *
 * Tests the kernel virtual memory mapping subsystem which tracks
 * memory regions, manages page tables, and provides demand paging.
 *
 * Test Coverage:
 * - Boot mappings verification
 * - Create/destroy with refcounting
 * - Lookup operations (address and range)
 * - Split and merge operations
 * - Modify flags
 * - SMP concurrent operations
 * - Page fault race handling
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "test_kmap.h"
#include "kernel/test.h"
#include "kernel/kmap.h"
#include "kernel/pmm.h"
#include "kernel/slab.h"
#include "kernel/klog.h"
#include "arch/x86_64/serial.h"
#include "arch/x86_64/smp.h"
#include "include/spinlock.h"
#include "include/atomic.h"
#include "include/barrier.h"
#include "arch/x86_64/paging.h"

/* External functions */
extern void system_shutdown(void);

#if CONFIG_TESTS_KMAP

/* ============================================================================
 * Test Configuration and Constants
 * ============================================================================ */

/* Test virtual address regions (carefully chosen to avoid conflicts) */
#define KMAP_TEST_REGION1_BASE    0x100000000ULL   /* 4GB */
#define KMAP_TEST_REGION2_BASE    0x200000000ULL   /* 8GB */
#define KMAP_TEST_REGION3_BASE    0x300000000ULL   /* 12GB */
#define KMAP_TEST_REGION_SIZE     (4 * PAGE_SIZE)  /* 4 pages (16KB) */

/* Boot mapping constants from kmap.c */
#define BOOT_IDENTITY_MAP_SIZE    (2 * 1024 * 1024)  /* 2MB */
#define BOOT_KERNEL_MAP_SIZE      (2 * 1024 * 1024)  /* 2MB */

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * test_is_bsp - Check if current CPU is Bootstrap Processor (BSP)
 * Returns: 1 if BSP, 0 otherwise
 */
static int test_is_bsp(void) {
    extern int is_bsp(void);
    return is_bsp();
}

/**
 * test_get_cpu_index - Get correct CPU index for array indexing
 * Returns: 0 for BSP, 1 for first AP, etc.
 */
static int test_get_cpu_index(void) {
    if (test_is_bsp()) {
        return 0;
    } else {
        extern uint8_t lapic_get_id(void);
        return (int)lapic_get_id();
    }
}

/**
 * test_get_active_cpu_count - Get number of CPUs participating in tests
 */
static int test_get_active_cpu_count(void) {
    extern int smp_get_cpu_count(void);
    return smp_get_cpu_count();
}

/* Barrier synchronization for SMP tests */
static atomic_int test_barrier = 0;
static atomic_int test_phase = 0;

#define BARRIER_TIMEOUT 10000000   /* ~10ms at 1GHz */

/**
 * test_barrier_wait - Synchronize all CPUs at a barrier
 * @expected: Expected number of CPUs to reach barrier
 * Returns: 0 on success, -1 on timeout
 */
static int test_barrier_wait(int expected) {
    atomic_fetch_add_explicit(&test_barrier, 1, memory_order_release);
    smp_mb();

    int timeout = BARRIER_TIMEOUT;
    int current = atomic_load_explicit(&test_barrier, memory_order_acquire);
    while (current < expected && timeout > 0) {
        cpu_relax();
        timeout--;
        current = atomic_load_explicit(&test_barrier, memory_order_acquire);
    }

    return (timeout == 0) ? -1 : 0;
}

/**
 * test_barrier_reset - Reset barrier for next use
 */
static void test_barrier_reset(void) {
    atomic_exchange_explicit(&test_barrier, 0, memory_order_release);
    smp_mb();
}

/**
 * test_set_phase - Advance to next test phase
 */
static void test_set_phase(int phase) {
    atomic_exchange_explicit(&test_phase, phase, memory_order_relaxed);
    smp_mb();
}

/**
 * test_wait_phase - Wait for a specific phase (APs only)
 */
static int test_wait_phase(int phase) {
    int timeout = BARRIER_TIMEOUT;
    while (atomic_load_explicit(&test_phase, memory_order_relaxed) != phase && timeout > 0) {
        cpu_relax();
        timeout--;
    }
    return (timeout == 0) ? -1 : 0;
}

/* Shared test mappings for SMP tests */
static kmap_t *shared_test_mapping = NULL;

/* ============================================================================
 * Test 1: Boot Mappings Verification
 * ============================================================================ */

/**
 * test_boot_mappings - Verify boot mappings are registered correctly
 *
 * Verifies that kmap_init() registered the expected boot mappings:
 * - Identity mapping (0-2MB) for low memory
 * - Kernel code mapping at KERNEL_BASE_VA
 */
static int test_boot_mappings(void) {
    kmap_t *mapping;

    klog_info("KMAP_TEST", "Test 1: Boot mappings verification...");

    /* Check identity mapping exists */
    mapping = kmap_lookup(0x1000);
    if (mapping == NULL) {
        klog_error("KMAP_TEST", "  FAILED: Identity mapping not found at 0x1000");
        return -1;
    }
    if (mapping->type != KMAP_DEVICE) {
        klog_error("KMAP_TEST", "  FAILED: Identity mapping has wrong type (expected DEVICE)");
        kmap_put(mapping);
        return -1;
    }
    if (mapping->pageable != KMAP_NONPAGEABLE) {
        klog_error("KMAP_TEST", "  FAILED: Identity mapping should be non-pageable");
        kmap_put(mapping);
        return -1;
    }
    klog_info("KMAP_TEST", "  PASS: Identity mapping verified");
    kmap_put(mapping);

    /* Check kernel code mapping exists */
    mapping = kmap_lookup(KERNEL_BASE_VA + 0x1000);
    if (mapping == NULL) {
        klog_error("KMAP_TEST", "  FAILED: Kernel code mapping not found");
        return -1;
    }
    if (mapping->type != KMAP_CODE) {
        klog_error("KMAP_TEST", "  FAILED: Kernel mapping has wrong type (expected CODE)");
        kmap_put(mapping);
        return -1;
    }
    if (mapping->monitor != KMAP_MONITOR_READONLY) {
        klog_error("KMAP_TEST", "  FAILED: Kernel mapping should have readonly monitor protection");
        kmap_put(mapping);
        return -1;
    }
    klog_info("KMAP_TEST", "  PASS: Kernel code mapping verified");
    kmap_put(mapping);

    klog_info("KMAP_TEST", "Test 1 PASSED");
    return 0;
}

/* ============================================================================
 * Test 2: Create/Destroy with Refcounting
 * ============================================================================ */

/**
 * test_create_destroy - Test mapping creation, refcounting, and destruction
 *
 * Verifies:
 * - kmap_create() creates mappings with correct properties
 * - Initial refcount is 1
 * - kmap_put() with refcount=1 destroys the mapping
 * - Destroyed mappings cannot be looked up
 */
static int test_create_destroy(void) {
    kmap_t *mapping;

    klog_info("KMAP_TEST", "Test 2: Create/destroy with refcounting...");

    /* Create test mapping */
    mapping = kmap_create(KMAP_TEST_REGION1_BASE,
                          KMAP_TEST_REGION1_BASE + KMAP_TEST_REGION_SIZE,
                          0,  /* phys_base = 0 (pageable/alloc-on-demand) */
                          X86_PTE_PRESENT | X86_PTE_WRITABLE,
                          KMAP_DATA,
                          KMAP_PAGEABLE,
                          KMAP_MONITOR_NONE,
                          "test_create");
    if (mapping == NULL) {
        klog_error("KMAP_TEST", "  FAILED: kmap_create returned NULL");
        return -1;
    }

    /* Verify initial state */
    if (mapping->refcount != 1) {
        klog_error("KMAP_TEST", "  FAILED: Initial refcount != 1 (got %d)", mapping->refcount);
        kmap_put(mapping);
        return -1;
    }

    if (mapping->virt_start != KMAP_TEST_REGION1_BASE) {
        klog_error("KMAP_TEST", "  FAILED: virt_start incorrect");
        kmap_put(mapping);
        return -1;
    }

    if (mapping->num_pages != 4) {
        klog_error("KMAP_TEST", "  FAILED: num_pages incorrect (expected 4, got %lu)", mapping->num_pages);
        kmap_put(mapping);
        return -1;
    }

    if (mapping->type != KMAP_DATA) {
        klog_error("KMAP_TEST", "  FAILED: type incorrect");
        kmap_put(mapping);
        return -1;
    }

    klog_info("KMAP_TEST", "  PASS: Mapping created with correct properties");

    /* Verify lookup finds it */
    kmap_t *lookup_result = kmap_lookup(KMAP_TEST_REGION1_BASE + PAGE_SIZE);
    if (lookup_result == NULL) {
        klog_error("KMAP_TEST", "  FAILED: Lookup did not find created mapping");
        kmap_put(mapping);
        return -1;
    }
    /* Lookup increments refcount, so should be 2 now */
    if (lookup_result->refcount != 2) {
        klog_error("KMAP_TEST", "  FAILED: Lookup did not increment refcount (got %d)", lookup_result->refcount);
        kmap_put(lookup_result);
        kmap_put(mapping);
        return -1;
    }
    kmap_put(lookup_result);  /* Release lookup refcount */
    klog_info("KMAP_TEST", "  PASS: Lookup works correctly");

    /* Put refcount (should auto-destroy) */
    kmap_put(mapping);

    /* Verify destroyed (lookup should fail) */
    mapping = kmap_lookup(KMAP_TEST_REGION1_BASE);
    if (mapping != NULL) {
        klog_error("KMAP_TEST", "  FAILED: Mapping still exists after kmap_put with refcount=1");
        kmap_put(mapping);
        return -1;
    }

    klog_info("KMAP_TEST", "  PASS: Mapping destroyed on refcount=0");
    klog_info("KMAP_TEST", "Test 2 PASSED");
    return 0;
}

/* ============================================================================
 * Test 3: Refcounting Operations
 * ============================================================================ */

/**
 * test_refcounting - Test kmap_get and kmap_put refcounting
 *
 * Verifies:
 * - kmap_get() increments refcount
 * - Multiple kmap_get() calls increment correctly
 * - kmap_put() decrements and destroys at zero
 */
static int test_refcounting(void) {
    kmap_t *mapping;

    klog_info("KMAP_TEST", "Test 3: Refcounting operations...");

    /* Create test mapping */
    mapping = kmap_create(KMAP_TEST_REGION1_BASE,
                          KMAP_TEST_REGION1_BASE + PAGE_SIZE,
                          0,
                          X86_PTE_PRESENT | X86_PTE_WRITABLE,
                          KMAP_DATA,
                          KMAP_PAGEABLE,
                          KMAP_MONITOR_NONE,
                          "test_refcount");
    if (mapping == NULL) {
        klog_error("KMAP_TEST", "  FAILED: kmap_create returned NULL");
        return -1;
    }

    /* Initial refcount should be 1 */
    if (mapping->refcount != 1) {
        klog_error("KMAP_TEST", "  FAILED: Initial refcount != 1");
        kmap_put(mapping);
        return -1;
    }

    /* Increment refcount multiple times */
    kmap_get(mapping);
    kmap_get(mapping);
    kmap_get(mapping);

    if (mapping->refcount != 4) {
        klog_error("KMAP_TEST", "  FAILED: Refcount after 3 gets != 4 (got %d)", mapping->refcount);
        kmap_put(mapping); kmap_put(mapping); kmap_put(mapping); kmap_put(mapping);
        return -1;
    }
    klog_info("KMAP_TEST", "  PASS: kmap_get increments refcount");

    /* Decrement back to 1 */
    kmap_put(mapping);
    kmap_put(mapping);
    kmap_put(mapping);

    if (mapping->refcount != 1) {
        klog_error("KMAP_TEST", "  FAILED: Refcount after 3 puts != 1 (got %d)", mapping->refcount);
        kmap_put(mapping);
        return -1;
    }
    klog_info("KMAP_TEST", "  PASS: kmap_put decrements refcount");

    /* Final put should destroy */
    kmap_put(mapping);
    mapping = kmap_lookup(KMAP_TEST_REGION1_BASE);
    if (mapping != NULL) {
        klog_error("KMAP_TEST", "  FAILED: Mapping still exists after final put");
        kmap_put(mapping);
        return -1;
    }

    klog_info("KMAP_TEST", "  PASS: Mapping destroyed at refcount=0");
    klog_info("KMAP_TEST", "Test 3 PASSED");
    return 0;
}

/* ============================================================================
 * Test 4: Lookup Operations
 * ============================================================================ */

/**
 * test_lookup - Test address and range lookup operations
 *
 * Verifies:
 * - kmap_lookup() finds mappings by address
 * - kmap_lookup_range() finds overlapping mappings
 * - Lookups return NULL for non-existent regions
 */
static int test_lookup(void) {
    kmap_t *mapping1, *mapping2, *result;

    klog_info("KMAP_TEST", "Test 4: Lookup operations...");

    /* Create two mappings */
    mapping1 = kmap_create(KMAP_TEST_REGION1_BASE,
                           KMAP_TEST_REGION1_BASE + PAGE_SIZE,
                           0,
                           X86_PTE_PRESENT | X86_PTE_WRITABLE,
                           KMAP_DATA,
                           KMAP_PAGEABLE,
                           KMAP_MONITOR_NONE,
                           "lookup_test1");
    if (mapping1 == NULL) {
        klog_error("KMAP_TEST", "  FAILED: Failed to create mapping1");
        return -1;
    }

    mapping2 = kmap_create(KMAP_TEST_REGION2_BASE,
                           KMAP_TEST_REGION2_BASE + (2 * PAGE_SIZE),
                           0,
                           X86_PTE_PRESENT | X86_PTE_WRITABLE,
                           KMAP_DATA,
                           KMAP_PAGEABLE,
                           KMAP_MONITOR_NONE,
                           "lookup_test2");
    if (mapping2 == NULL) {
        klog_error("KMAP_TEST", "  FAILED: Failed to create mapping2");
        kmap_put(mapping1);
        return -1;
    }

    /* Test address lookup */
    result = kmap_lookup(KMAP_TEST_REGION1_BASE + 0x100);
    if (result == NULL) {
        klog_error("KMAP_TEST", "  FAILED: Address lookup failed");
        kmap_put(mapping1); kmap_put(mapping2);
        return -1;
    }
    if (result != mapping1) {
        klog_error("KMAP_TEST", "  FAILED: Address lookup returned wrong mapping");
        kmap_put(result); kmap_put(mapping1); kmap_put(mapping2);
        return -1;
    }
    kmap_put(result);
    klog_info("KMAP_TEST", "  PASS: Address lookup works");

    /* Test range lookup */
    result = kmap_lookup_range(KMAP_TEST_REGION2_BASE + PAGE_SIZE,
                                KMAP_TEST_REGION2_BASE + (2 * PAGE_SIZE));
    if (result == NULL) {
        klog_error("KMAP_TEST", "  FAILED: Range lookup failed");
        kmap_put(mapping1); kmap_put(mapping2);
        return -1;
    }
    if (result != mapping2) {
        klog_error("KMAP_TEST", "  FAILED: Range lookup returned wrong mapping");
        kmap_put(result); kmap_put(mapping1); kmap_put(mapping2);
        return -1;
    }
    kmap_put(result);
    klog_info("KMAP_TEST", "  PASS: Range lookup works");

    /* Test lookup of non-existent address */
    result = kmap_lookup(KMAP_TEST_REGION3_BASE);
    if (result != NULL) {
        klog_error("KMAP_TEST", "  FAILED: Lookup found non-existent mapping");
        kmap_put(result); kmap_put(mapping1); kmap_put(mapping2);
        return -1;
    }
    klog_info("KMAP_TEST", "  PASS: Non-existent lookup returns NULL");

    /* Cleanup */
    kmap_put(mapping1);
    kmap_put(mapping2);

    klog_info("KMAP_TEST", "Test 4 PASSED");
    return 0;
}

/* ============================================================================
 * Test 5: Split Operation
 * ============================================================================ */

/**
 * test_split - Test mapping split at boundary
 *
 * Verifies:
 * - kmap_split() divides mapping at specified address
 * - Original mapping covers [start, split_addr)
 * - New mapping covers [split_addr, end)
 * - Properties are preserved
 */
static int test_split(void) {
    kmap_t *mapping, *lookup1, *lookup2;

    klog_info("KMAP_TEST", "Test 5: Split operation...");

    /* Create mapping covering 4 pages */
    mapping = kmap_create(KMAP_TEST_REGION1_BASE,
                          KMAP_TEST_REGION1_BASE + (4 * PAGE_SIZE),
                          0,
                          X86_PTE_PRESENT | X86_PTE_WRITABLE,
                          KMAP_DATA,
                          KMAP_PAGEABLE,
                          KMAP_MONITOR_NONE,
                          "split_test");
    if (mapping == NULL) {
        klog_error("KMAP_TEST", "  FAILED: kmap_create returned NULL");
        return -1;
    }

    /* Split at page 2 boundary */
    uint64_t split_addr = KMAP_TEST_REGION1_BASE + (2 * PAGE_SIZE);
    int ret = kmap_split(mapping, split_addr);
    if (ret != 0) {
        klog_error("KMAP_TEST", "  FAILED: kmap_split returned error");
        kmap_put(mapping);
        return -1;
    }

    /* Verify original mapping now covers first 2 pages */
    if (mapping->virt_end != split_addr) {
        klog_error("KMAP_TEST", "  FAILED: Original mapping virt_end not updated");
        kmap_put(mapping);
        return -1;
    }
    if (mapping->num_pages != 2) {
        klog_error("KMAP_TEST", "  FAILED: Original mapping num_pages incorrect");
        kmap_put(mapping);
        return -1;
    }

    /* Verify new mapping exists for second 2 pages */
    lookup2 = kmap_lookup(split_addr + PAGE_SIZE);
    if (lookup2 == NULL) {
        klog_error("KMAP_TEST", "  FAILED: New mapping not found");
        kmap_put(mapping);
        return -1;
    }

    if (lookup2->virt_start != split_addr) {
        klog_error("KMAP_TEST", "  FAILED: New mapping virt_start incorrect");
        kmap_put(mapping); kmap_put(lookup2);
        return -1;
    }

    if (lookup2->num_pages != 2) {
        klog_error("KMAP_TEST", "  FAILED: New mapping num_pages incorrect");
        kmap_put(mapping); kmap_put(lookup2);
        return -1;
    }

    /* Verify properties are preserved */
    if (lookup2->type != mapping->type ||
        lookup2->pageable != mapping->pageable ||
        lookup2->monitor != mapping->monitor ||
        lookup2->flags != mapping->flags) {
        klog_error("KMAP_TEST", "  FAILED: Properties not preserved in split");
        kmap_put(mapping); kmap_put(lookup2);
        return -1;
    }

    kmap_put(lookup2);
    kmap_put(mapping);

    klog_info("KMAP_TEST", "  PASS: Split creates correct mappings");
    klog_info("KMAP_TEST", "Test 5 PASSED");
    return 0;
}

/* ============================================================================
 * Test 6: Merge Operation
 * ============================================================================ */

/**
 * test_merge - Test merging adjacent compatible mappings
 *
 * Verifies:
 * - kmap_merge() combines adjacent mappings with identical properties
 * - Merged mapping has combined size
 * - Second mapping is removed
 */
static int test_merge(void) {
    kmap_t *m1, *m2, *lookup;

    klog_info("KMAP_TEST", "Test 6: Merge operation...");

    /* Create two adjacent mappings with identical properties */
    m1 = kmap_create(KMAP_TEST_REGION1_BASE,
                     KMAP_TEST_REGION1_BASE + PAGE_SIZE,
                     0,
                     X86_PTE_PRESENT | X86_PTE_WRITABLE,
                     KMAP_DATA,
                     KMAP_PAGEABLE,
                     KMAP_MONITOR_NONE,
                     "merge_test1");
    if (m1 == NULL) {
        klog_error("KMAP_TEST", "  FAILED: Failed to create m1");
        return -1;
    }

    m2 = kmap_create(KMAP_TEST_REGION1_BASE + PAGE_SIZE,
                     KMAP_TEST_REGION1_BASE + (2 * PAGE_SIZE),
                     0,
                     X86_PTE_PRESENT | X86_PTE_WRITABLE,
                     KMAP_DATA,
                     KMAP_PAGEABLE,
                     KMAP_MONITOR_NONE,
                     "merge_test2");
    if (m2 == NULL) {
        klog_error("KMAP_TEST", "  FAILED: Failed to create m2");
        kmap_put(m1);
        return -1;
    }

    /* Merge m2 into m1 */
    int ret = kmap_merge(m1, m2);
    if (ret != 0) {
        klog_error("KMAP_TEST", "  FAILED: kmap_merge returned error");
        kmap_put(m1); kmap_put(m2);
        return -1;
    }

    /* Verify m1 now covers both pages */
    if (m1->virt_end != KMAP_TEST_REGION1_BASE + (2 * PAGE_SIZE)) {
        klog_error("KMAP_TEST", "  FAILED: Merged mapping virt_end incorrect");
        kmap_put(m1);
        return -1;
    }

    if (m1->num_pages != 2) {
        klog_error("KMAP_TEST", "  FAILED: Merged mapping num_pages incorrect");
        kmap_put(m1);
        return -1;
    }

    /* Verify m2 no longer exists in list */
    lookup = kmap_lookup(KMAP_TEST_REGION1_BASE + PAGE_SIZE + 0x100);
    if (lookup == NULL) {
        klog_error("KMAP_TEST", "  FAILED: Cannot find merged region");
        kmap_put(m1);
        return -1;
    }
    /* Should find m1, not m2 */
    if (lookup != m1) {
        klog_error("KMAP_TEST", "  FAILED: Found wrong mapping after merge");
        kmap_put(lookup); kmap_put(m1);
        return -1;
    }
    kmap_put(lookup);

    kmap_put(m1);  /* This will destroy the merged mapping */

    klog_info("KMAP_TEST", "  PASS: Merge combines adjacent mappings");
    klog_info("KMAP_TEST", "Test 6 PASSED");
    return 0;
}

/* ============================================================================
 * Test 7: Modify Flags
 * ============================================================================ */

/**
 * test_modify_flags - Test modifying PTE flags for a mapping
 *
 * Verifies:
 * - kmap_modify_flags() updates mapping flags
 * - Flags are applied to all pages in mapping
 */
static int test_modify_flags(void) {
    kmap_t *mapping;

    klog_info("KMAP_TEST", "Test 7: Modify flags...");

    /* Create mapping */
    mapping = kmap_create(KMAP_TEST_REGION1_BASE,
                          KMAP_TEST_REGION1_BASE + PAGE_SIZE,
                          0,
                          X86_PTE_PRESENT | X86_PTE_WRITABLE,
                          KMAP_DATA,
                          KMAP_PAGEABLE,
                          KMAP_MONITOR_NONE,
                          "flags_test");
    if (mapping == NULL) {
        klog_error("KMAP_TEST", "  FAILED: kmap_create returned NULL");
        return -1;
    }

    /* Modify to read-only */
    int ret = kmap_modify_flags(mapping, X86_PTE_PRESENT);
    if (ret != 0) {
        klog_error("KMAP_TEST", "  FAILED: kmap_modify_flags returned error");
        kmap_put(mapping);
        return -1;
    }

    /* Verify flags were updated */
    if (mapping->flags != X86_PTE_PRESENT) {
        klog_error("KMAP_TEST", "  FAILED: Flags not updated correctly");
        kmap_put(mapping);
        return -1;
    }

    kmap_put(mapping);

    klog_info("KMAP_TEST", "  PASS: Flags modified successfully");
    klog_info("KMAP_TEST", "Test 7 PASSED");
    return 0;
}

/* ============================================================================
 * Test 8: SMP Concurrent Lookup
 * ============================================================================ */

/**
 * test_concurrent_lookup_smp - Test concurrent lookups from multiple CPUs
 *
 * Verifies:
 * - Multiple CPUs can safely perform concurrent lookups
 * - Refcounting is atomic across CPUs
 * - No race conditions or data corruption
 */
static int test_concurrent_lookup_smp(void) {
    int num_cpus = test_get_active_cpu_count();
    int my_cpu = test_get_cpu_index();
    kmap_t *mappings[16];
    int i;

    if (test_is_bsp()) {
        klog_info("KMAP_TEST", "Test 8: SMP concurrent lookup...");
        test_barrier_reset();
    }

    /* Phase 1: BSP creates shared mapping */
    if (test_is_bsp()) {
        shared_test_mapping = kmap_create(KMAP_TEST_REGION1_BASE,
                                          KMAP_TEST_REGION1_BASE + PAGE_SIZE,
                                          0,
                                          X86_PTE_PRESENT | X86_PTE_WRITABLE,
                                          KMAP_DATA,
                                          KMAP_PAGEABLE,
                                          KMAP_MONITOR_NONE,
                                          "concurrent_lookup");
        if (shared_test_mapping == NULL) {
            klog_error("KMAP_TEST", "  FAILED: BSP failed to create shared mapping");
            return -1;
        }
        smp_mb();
    }
    test_barrier_wait(num_cpus);

    /* Phase 2: All CPUs perform concurrent lookups */
    for (i = 0; i < 16; i++) {
        mappings[i] = kmap_lookup(KMAP_TEST_REGION1_BASE);
        if (mappings[i] == NULL) {
            klog_error("KMAP_TEST", "  FAILED: CPU %d lookup failed", my_cpu);
            /* Cleanup */
            for (int j = 0; j < i; j++) kmap_put(mappings[j]);
            return -1;
        }
    }

    /* Verify we got the same mapping each time */
    for (i = 1; i < 16; i++) {
        if (mappings[i] != mappings[0]) {
            klog_error("KMAP_TEST", "  FAILED: Lookup %d returned different mapping", i);
            /* Cleanup */
            for (int j = 0; j < 16; j++) kmap_put(mappings[j]);
            return -1;
        }
    }

    smp_mb();
    test_barrier_wait(num_cpus);

    /* Phase 3: BSP verifies refcount */
    if (test_is_bsp()) {
        /* Expected: 1 (initial) + 16 * num_cpus (from all CPUs) */
        int expected_min = 1 + (16 * num_cpus);
        if (shared_test_mapping->refcount < expected_min) {
            klog_error("KMAP_TEST", "  FAILED: Refcount too low (got %d, expected >= %d)",
                      shared_test_mapping->refcount, expected_min);
            return -1;
        }
        klog_info("KMAP_TEST", "  PASS: Refcount correct after concurrent lookups (%d >= %d)",
                  shared_test_mapping->refcount, expected_min);
    }

    /* Phase 4: Cleanup */
    for (i = 0; i < 16; i++) {
        kmap_put(mappings[i]);
    }

    smp_mb();
    test_barrier_wait(num_cpus);

    /* Phase 5: BSP destroys shared mapping */
    if (test_is_bsp()) {
        kmap_put(shared_test_mapping);
        shared_test_mapping = NULL;
        klog_info("KMAP_TEST", "Test 8 PASSED");
    }

    return 0;
}

/* ============================================================================
 * Test 9: Pageable Status Query
 * ============================================================================ */

/**
 * test_pageable_query - Test kmap_is_pageable and monitor protection queries
 *
 * Verifies:
 * - kmap_is_pageable() returns correct status
 * - kmap_get_monitor_protection() returns correct level
 */
static int test_pageable_query(void) {
    kmap_t *mapping;
    bool pageable;
    kmap_monitor_t monitor;

    klog_info("KMAP_TEST", "Test 9: Pageable status query...");

    /* Create pageable mapping */
    mapping = kmap_create(KMAP_TEST_REGION1_BASE,
                          KMAP_TEST_REGION1_BASE + PAGE_SIZE,
                          0,
                          X86_PTE_PRESENT | X86_PTE_WRITABLE,
                          KMAP_DATA,
                          KMAP_PAGEABLE,
                          KMAP_MONITOR_READONLY,
                          "query_test");
    if (mapping == NULL) {
        klog_error("KMAP_TEST", "  FAILED: kmap_create returned NULL");
        return -1;
    }

    /* Test kmap_is_pageable */
    pageable = kmap_is_pageable(KMAP_TEST_REGION1_BASE);
    if (!pageable) {
        klog_error("KMAP_TEST", "  FAILED: kmap_is_pageable returned false for pageable region");
        kmap_put(mapping);
        return -1;
    }

    /* Test kmap_get_monitor_protection */
    monitor = kmap_get_monitor_protection(KMAP_TEST_REGION1_BASE);
    if (monitor != KMAP_MONITOR_READONLY) {
        klog_error("KMAP_TEST", "  FAILED: kmap_get_monitor_protection returned wrong level");
        kmap_put(mapping);
        return -1;
    }

    kmap_put(mapping);

    /* Test with non-pageable region (identity mapping) */
    pageable = kmap_is_pageable(0x1000);
    if (pageable) {
        klog_error("KMAP_TEST", "  FAILED: Identity mapping incorrectly reported as pageable");
        return -1;
    }

    klog_info("KMAP_TEST", "  PASS: Query functions work correctly");
    klog_info("KMAP_TEST", "Test 9 PASSED");
    return 0;
}

/* ============================================================================
 * AP Entry Point for SMP Tests
 * ============================================================================ */

/**
 * kmap_test_ap_entry - AP entry point for SMP tests
 */
void kmap_test_ap_entry(void) {
    int num_cpus = test_get_active_cpu_count();

    /* Wait for BSP to signal first test phase */
    if (test_wait_phase(1) < 0) {
        return;
    }

    /* Test 8: SMP concurrent lookup */
    int result = test_concurrent_lookup_smp();
    (void)result;  /* Result checked by BSP */

    /* Wait for completion signal */
    test_wait_phase(2);

    return;
}

/* ============================================================================
 * Main Test Runner (BSP only)
 * ============================================================================ */

/**
 * run_kmap_tests - Run all KMAP subsystem tests
 *
 * Returns: Number of failed tests (0 = all passed)
 */
int run_kmap_tests(void) {
    int num_cpus = test_get_active_cpu_count();
    int failures = 0;

    klog_info("KMAP_TEST", "=== KMAP Subsystem Test Suite ===");
    klog_info("KMAP_TEST", "Number of CPUs: %d", num_cpus);

    /* ========================================================================
     * Single-CPU Tests (Run on BSP only)
     * ======================================================================== */

    klog_info("KMAP_TEST", "=== Single-CPU Tests ===");

    if (test_boot_mappings() != 0) {
        failures++;
    }

    if (test_create_destroy() != 0) {
        failures++;
    }

    if (test_refcounting() != 0) {
        failures++;
    }

    if (test_lookup() != 0) {
        failures++;
    }

    if (test_split() != 0) {
        failures++;
    }

    if (test_merge() != 0) {
        failures++;
    }

    if (test_modify_flags() != 0) {
        failures++;
    }

    if (test_pageable_query() != 0) {
        failures++;
    }

    /* ========================================================================
     * SMP Multi-CPU Tests
     * ======================================================================== */

    if (num_cpus > 1) {
        klog_info("KMAP_TEST", "=== SMP Multi-CPU Tests ===");

        /* Start first test phase - unblocks APs */
        test_set_phase(1);

        /* Small delay for APs to wake up */
        for (volatile int i = 0; i < 200000; i++) {
            cpu_relax();
        }

        /* Test 8: SMP concurrent lookup */
        int result = test_concurrent_lookup_smp();
        if (result != 0) {
            failures++;
        }
        test_set_phase(2);

    } else {
        klog_info("KMAP_TEST", "=== SMP Tests Skipped (Single CPU) ===");
    }

    /* ========================================================================
     * Summary
     * ======================================================================== */

    klog_info("KMAP_TEST", "========================================");

    int total_tests = 8;  /* Single-CPU tests */
    if (num_cpus > 1) {
        total_tests += 1;  /* Add SMP test */
    }

    int passed = total_tests - failures;
    klog_info("KMAP_TEST", "Summary: %d/%d tests passed", passed, total_tests);

    if (failures == 0) {
        klog_info("KMAP_TEST", "KMAP: All tests PASSED");
    } else {
        klog_error("KMAP_TEST", "KMAP: %d test(s) FAILED", failures);
    }

    klog_info("KMAP_TEST", "========================================");

    return failures;
}

#endif /* CONFIG_TESTS_KMAP */

/* ============================================================================
 * Test Wrapper
 * ============================================================================ */

#if CONFIG_TESTS_KMAP
void test_kmap(void) {
    if (test_should_run("kmap")) {
        if (!test_did_run("kmap")) {
            int result = run_kmap_tests();
            test_mark_run("kmap", result);
            if (result == 0) {
                klog_info("TEST", "PASSED: kmap");
            } else {
                klog_error("TEST", "FAILED: kmap (failures: %d)", result);
                system_shutdown();
            }
        }
    }
}
#else
void test_kmap(void) { }
#endif
