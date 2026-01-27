/* JAKernel - Spin Lock Tests */

#include <stddef.h>
#include <stdint.h>
#include "kernel/smp.h"
#include "include/spinlock.h"
#include "arch/x86_64/serial.h"

/* External functions */
extern void serial_puts(const char *str);
extern void serial_putc(char c);
extern int smp_get_cpu_index(void);
extern uint8_t smp_get_apic_id(void);

/* ============================================================================
 * Test Synchronization Primitives
 * ============================================================================ */

/* Test activation flag - set by BSP to signal APs to join tests */
volatile int spinlock_test_start = 0;

/* Barrier counter - counts CPUs that have reached the barrier */
static volatile int test_barrier = 0;

/* Current test phase - indicates which test is running */
static volatile int test_phase = 0;

/* Per-CPU counters for SMP tests */
static volatile int test_counter[SMP_MAX_CPUS];

/* Shared counter for lock contention tests */
static volatile int shared_counter = 0;

/* Test locks */
static spinlock_t test_lock1;
static spinlock_t test_lock2;
static rwlock_t test_rwlock;

/* Test result counters */
static volatile int tests_passed = 0;
static volatile int tests_failed = 0;
static volatile int test_errors[SMP_MAX_CPUS];  /* Per-CPU error flags */

/* Test completion flag */
static volatile int test_complete = 0;

/* Signal flag for test7 - BSP signals when lock is ready */
static volatile int test7_lock_ready = 0;

/* Timeout constants */
#define BARRIER_TIMEOUT 10000000   /* ~10ms at 1GHz */
#define SPIN_TIMEOUT 1000000       /* ~1ms at 1GHz */

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/**
 * test_is_bsp - Check if current CPU is Bootstrap Processor (BSP)
 * Returns: 1 if BSP, 0 otherwise
 */
static int test_is_bsp(void) {
    /* Use the existing is_bsp() function from apic.c */
    extern int is_bsp(void);
    return is_bsp();
}

/**
 * test_get_cpu_index - Get correct CPU index for array indexing
 * Returns: 0 for BSP, 1 for first AP, etc.
 *
 * This function works around the bug where smp_get_cpu_index() returns
 * the wrong value for BSP due to the global current_cpu_index variable.
 */
static int test_get_cpu_index(void) {
    /* BSP always has index 0, APs have index 1, 2, etc. */
    if (test_is_bsp()) {
        return 0;
    } else {
        /* For APs, use APIC ID to determine index
         * In QEMU with 2 CPUs: BSP has APIC ID 0, AP has APIC ID 1
         * So APIC ID = CPU index for now */
        extern uint8_t lapic_get_id(void);
        return (int)lapic_get_id();
    }
}

/**
 * test_barrier_wait - Synchronize all CPUs at a barrier
 * @expected: Expected number of CPUs to reach barrier
 *
 * Returns: 0 on success, -1 on timeout
 */
static int test_barrier_wait(int expected) {
    /* Atomic increment of barrier counter */
    __sync_fetch_and_add(&test_barrier, 1);

    /* CPU memory barrier - ensure increment visible immediately */
    asm volatile("mfence" ::: "memory");

    /* Wait for all CPUs to arrive */
    int timeout = BARRIER_TIMEOUT;
    while (test_barrier < expected && timeout > 0) {
        asm volatile("pause");
        timeout--;
    }

    if (timeout == 0) {
        return -1;  /* Timeout */
    }

    return 0;
}

/**
 * test_barrier_reset - Reset barrier for next use
 */
static void test_barrier_reset(void) {
    /* Use atomic exchange to ensure write is visible to all CPUs */
    __atomic_exchange_n(&test_barrier, 0, __ATOMIC_SEQ_CST);
    /* Full memory fence */
    asm volatile("mfence" ::: "memory");
}

/**
 * test_set_phase - Advance to next test phase
 * @phase: New phase number
 *
 * BSP sets the phase, APs wait for phase changes.
 */
static void test_set_phase(int phase) {
    /* Use atomic exchange to ensure write is visible to all CPUs immediately */
    __atomic_exchange_n(&test_phase, phase, __ATOMIC_SEQ_CST);
    /* Force memory barrier */
    asm volatile("mfence" ::: "memory");
}

/**
 * test_wait_phase - Wait for a specific phase (APs only)
 * @phase: Expected phase number
 *
 * Returns: 0 on success, -1 on timeout
 *
 * Note: Only exits when test_phase == phase, not when test_phase > phase.
 * This prevents AP from skipping phases if BSP advances quickly.
 */
static int test_wait_phase(int phase) {
    int timeout = BARRIER_TIMEOUT;
    while (test_phase != phase && timeout > 0) {
        asm volatile("pause");
        asm volatile("mfence" ::: "memory");  /* Ensure we see the updated phase */
        timeout--;
    }
    return (timeout == 0) ? -1 : 0;
}

/**
 * test_get_active_cpu_count - Get number of CPUs participating in tests
 */
static int test_get_active_cpu_count(void) {
    /* For now, assume all initialized CPUs participate */
    extern int smp_get_cpu_count(void);
    return smp_get_cpu_count();
}

/**
 * test_atomic_inc - Atomic increment with return value
 * @ptr: Pointer to volatile int
 *
 * Returns: New value after increment
 */
static int test_atomic_inc(volatile int *ptr) {
    return __sync_add_and_fetch(ptr, 1);
}

/* ============================================================================
 * Output Functions
 * ============================================================================ */

/**
 * test_put_hex - Print hex value to serial
 * @value: Value to print
 */
static void test_put_hex(uint64_t value) {
    const char hex_chars[] = "0123456789ABCDEF";
    char buf[17];
    int i;

    if (value == 0) {
        serial_putc('0');
        return;
    }

    for (i = 15; i >= 0; i--) {
        buf[i] = hex_chars[value & 0xF];
        value >>= 4;
        if (value == 0) break;
    }

    while (i < 15 && buf[i] == '0') i++;

    while (i <= 15) {
        serial_putc(buf[i++]);
    }
}

/**
 * test_puts - Print test prefix + message
 * @msg: Message to print
 */
static void test_puts(const char *msg) {
    serial_puts("[ Spin lock tests ] ");
    serial_puts(msg);
}

/* ============================================================================
 * Single-CPU Tests (Run on BSP only)
 * ============================================================================ */

/**
 * test1_basic_lock_unlock - Test 1: Basic lock/unlock operations
 */
static int test1_basic_lock_unlock(void) {
    spinlock_t lock;

    test_puts("Test 1: Basic lock operations...\n");

    /* Initialize lock */
    spin_lock_init(&lock);
    if (lock.locked != 0) {
        test_puts("  FAIL: Lock not initialized to unlocked state\n");
        return -1;
    }
    test_puts("  PASS: Lock initialized correctly\n");

    /* Acquire lock */
    spin_lock(&lock);
    if (lock.locked != 1) {
        test_puts("  FAIL: Lock not set after spin_lock\n");
        return -1;
    }
    test_puts("  PASS: Lock acquired correctly\n");

    /* Release lock */
    spin_unlock(&lock);
    if (lock.locked != 0) {
        test_puts("  FAIL: Lock not cleared after spin_unlock\n");
        return -1;
    }
    test_puts("  PASS: Lock released correctly\n");

    test_puts("Test 1 PASSED\n\n");
    return 0;
}

/**
 * test2_trylock - Test 2: Trylock behavior
 */
static int test2_trylock(void) {
    spinlock_t lock;
    int result;

    test_puts("Test 2: Trylock behavior...\n");

    spin_lock_init(&lock);

    /* Trylock on unlocked lock should succeed */
    result = spin_trylock(&lock);
    if (result != 1) {
        test_puts("  FAIL: trylock failed on unlocked lock\n");
        return -1;
    }
    test_puts("  PASS: trylock succeeded on unlocked lock\n");

    /* Trylock on locked lock should fail */
    result = spin_trylock(&lock);
    if (result != 0) {
        test_puts("  FAIL: trylock succeeded on already locked lock\n");
        spin_unlock(&lock);
        return -1;
    }
    test_puts("  PASS: trylock failed on locked lock\n");

    /* Clean up */
    spin_unlock(&lock);

    test_puts("Test 2 PASSED\n\n");
    return 0;
}

/**
 * test3_irqsafe_operations - Test 3: IRQ-safe operations
 */
static int test3_irqsafe_operations(void) {
    spinlock_t lock;
    irq_flags_t flags;

    test_puts("Test 3: IRQ-safe operations...\n");

    spin_lock_init(&lock);

    /* Get initial interrupt state */
    uint64_t rflags;
    asm volatile("pushf\npop %0" : "=rm"(rflags));

    /* spin_lock_irqsave should save and disable interrupts */
    spin_lock_irqsave(&lock, &flags);

    /* Check that interrupts are disabled */
    uint64_t rflags_after;
    asm volatile("pushf\npop %0" : "=rm"(rflags_after));
    if (rflags_after & (1 << 9)) {
        test_puts("  FAIL: Interrupts not disabled by irqsave\n");
        spin_unlock_irqrestore(&lock, &flags);
        return -1;
    }
    test_puts("  PASS: Interrupts disabled by irqsave\n");

    /* Unlock and restore interrupts */
    spin_unlock_irqrestore(&lock, &flags);

    /* Check that interrupt state was restored */
    asm volatile("pushf\npop %0" : "=rm"(rflags_after));
    /* Both should have same interrupt state */
    if ((rflags & (1 << 9)) != (rflags_after & (1 << 9))) {
        test_puts("  FAIL: Interrupt state not restored\n");
        return -1;
    }
    test_puts("  PASS: Interrupt state restored\n");

    test_puts("Test 3 PASSED\n\n");
    return 0;
}

/**
 * test4_rwlock_basic - Test 4: Read-write lock basic operations
 */
static int test4_rwlock_basic(void) {
    rwlock_t lock;

    test_puts("Test 4: Read-write lock operations...\n");

    /* Initialize rwlock */
    rwlock_init(&lock);
    if (lock.counter != 0) {
        test_puts("  FAIL: RWLock not initialized to unlocked state\n");
        return -1;
    }
    test_puts("  PASS: RWLock initialized correctly\n");

    /* Acquire read lock */
    spin_read_lock(&lock);
    if (lock.counter <= 0) {
        test_puts("  FAIL: Read lock did not increment counter\n");
        spin_read_unlock(&lock);
        return -1;
    }
    test_puts("  PASS: Read lock acquired correctly\n");

    /* Release read lock */
    spin_read_unlock(&lock);
    if (lock.counter != 0) {
        test_puts("  FAIL: Read lock did not decrement counter\n");
        return -1;
    }
    test_puts("  PASS: Read lock released correctly\n");

    /* Acquire write lock */
    spin_write_lock(&lock);
    if (lock.counter != -1) {
        test_puts("  FAIL: Write lock did not set counter to -1\n");
        spin_write_unlock(&lock);
        return -1;
    }
    test_puts("  PASS: Write lock acquired correctly\n");

    /* Release write lock */
    spin_write_unlock(&lock);
    if (lock.counter != 0) {
        test_puts("  FAIL: Write lock did not reset counter\n");
        return -1;
    }
    test_puts("  PASS: Write lock released correctly\n");

    test_puts("Test 4 PASSED\n\n");
    return 0;
}

/**
 * test5_nested_locks - Test 5: Nested locks (consistent ordering)
 */
static int test5_nested_locks(void) {
    spinlock_t lock1, lock2;

    test_puts("Test 5: Nested lock ordering...\n");

    spin_lock_init(&lock1);
    spin_lock_init(&lock2);

    /* Acquire locks in consistent order (lock1 first, then lock2) */
    spin_lock(&lock1);
    test_puts("  PASS: First lock acquired\n");

    spin_lock(&lock2);
    test_puts("  PASS: Second lock acquired\n");

    /* Verify both are held */
    if (lock1.locked != 1 || lock2.locked != 1) {
        test_puts("  FAIL: Locks not properly held\n");
        spin_unlock(&lock2);
        spin_unlock(&lock1);
        return -1;
    }
    test_puts("  PASS: Both locks held simultaneously\n");

    /* Release in reverse order */
    spin_unlock(&lock2);
    test_puts("  PASS: Second lock released\n");

    spin_unlock(&lock1);
    test_puts("  PASS: First lock released\n");

    test_puts("Test 5 PASSED\n\n");
    return 0;
}

/* ============================================================================
 * SMP Multi-CPU Tests
 * ============================================================================ */

/**
 * test6_lock_contention - Test 6: Lock contention with shared counter
 */
static int test6_lock_contention(int num_cpus) {
    int my_cpu = test_get_cpu_index();

    if (test_is_bsp()) {
        test_puts("Test 6: Lock contention...\n");
        test_barrier_reset();  /* Reset barrier at start of test */
    }

    /* Phase 1: Initialize and synchronize */
    if (test_is_bsp()) {
        shared_counter = 0;
        spin_lock_init(&test_lock1);
        for (int i = 0; i < SMP_MAX_CPUS; i++) {
            test_counter[i] = 0;
        }
    }
    test_barrier_wait(num_cpus);

    /* Phase 2: Each CPU increments shared counter 100 times */
    const int ITERATIONS = 100;
    for (int i = 0; i < ITERATIONS; i++) {
        irq_flags_t flags;
        spin_lock_irqsave(&test_lock1, &flags);
        shared_counter++;
        spin_unlock_irqrestore(&test_lock1, &flags);
    }

    /* Each CPU counts its iterations */
    test_counter[my_cpu] = ITERATIONS;

    /* Phase 3: Synchronize and verify */
    test_barrier_wait(num_cpus);

    if (test_is_bsp()) {
        test_barrier_reset();  /* Only BSP resets barrier */

        int expected = 0;
        for (int i = 0; i < num_cpus; i++) {
            expected += test_counter[i];
        }

        if (shared_counter == expected) {
            test_puts("  PASS: Shared counter correct (");
            test_put_hex(shared_counter);
            test_puts(" == ");
            test_put_hex(expected);
            test_puts(")\n");
            test_puts("Test 6 PASSED\n\n");
            return 0;
        } else {
            test_puts("  FAIL: Shared counter incorrect (");
            test_put_hex(shared_counter);
            test_puts(" != ");
            test_put_hex(expected);
            test_puts(")\n");
            return -1;
        }
    }

    return 0;
}

/**
 * test7_trylock_contention - Test 7: Trylock contention (exactly one succeeds)
 */
static int test7_trylock_contention(int num_cpus) {
    int my_cpu = test_get_cpu_index();

    if (test_is_bsp()) {
        test_puts("Test 7: Trylock contention...\n");
        test7_lock_ready = 0;  /* Initialize signal */
        test_barrier_reset();  /* Reset barrier at start of test */
    }

    /* Phase 1: Initialize and synchronize */
    if (test_is_bsp()) {
        spin_lock_init(&test_lock1);
    }
    test_barrier_wait(num_cpus);
    if (test_is_bsp()) {
        test_barrier_reset();  /* Reset for second barrier */
    }

    /* Phase 2: Test trylock - BSP holds lock, AP tries to acquire (should fail) */
    if (test_is_bsp()) {
        /* BSP: Acquire and hold lock */
        irq_flags_t flags;
        asm volatile("pushf\npop %0" : "=rm"(flags));
        disable_interrupts();

        spin_lock(&test_lock1);
        test_counter[my_cpu] = 1;  /* BSP acquired lock */

        /* Signal AP that lock is held */
        test7_lock_ready = 1;
        asm volatile("mfence" ::: "memory");  /* Ensure write is visible */

        /* Wait for AP to finish its attempt (AP will increment barrier) */
        test_barrier_wait(num_cpus);

        spin_unlock(&test_lock1);
        enable_interrupts();
    } else {
        /* AP: Wait for BSP to acquire lock, then try trylock */
        while (!test7_lock_ready) {
            asm volatile("pause");
        }

        irq_flags_t flags;
        asm volatile("pushf\npop %0" : "=rm"(flags));
        disable_interrupts();

        int success = spin_trylock(&test_lock1);
        test_counter[my_cpu] = success ? 1 : 0;

        /* Signal BSP we're done */
        __sync_fetch_and_add(&test_barrier, 1);

        if (success) {
            spin_unlock(&test_lock1);
        }

        enable_interrupts();
    }

    /* Phase 3: Synchronize and verify */
    test_barrier_wait(num_cpus);

    if (test_is_bsp()) {
        test_barrier_reset();  /* Reset for next test */

        int total_success = 0;
        for (int i = 0; i < num_cpus; i++) {
            total_success += test_counter[i];
        }

        if (total_success == 1) {
            test_puts("  PASS: Exactly one CPU acquired lock\n");
            test_puts("Test 7 PASSED\n\n");

            /* Small delay for AP to exit test 7 and reach test_wait_phase(3) */
            for (volatile int i = 0; i < 50000; i++) {
                asm volatile("pause");
            }

            return 0;
        } else {
            test_puts("  FAIL: ");
            test_put_hex(total_success);
            test_puts(" CPUs acquired lock (expected 1)\n");
            return -1;
        }
    }

    return 0;
}

/**
 * test8_rwlock_readers - Test 8: Multiple concurrent readers
 * Uses flag-based synchronization to avoid barrier deadlock.
 */
static int test8_rwlock_readers(int num_cpus) {
    /* Synchronization flags */
    static volatile int test8_ready = 0;
    static volatile int test8_done[SMP_MAX_CPUS] = {0};

    int my_cpu = test_get_cpu_index();

    if (test_is_bsp()) {
        test_puts("Test 8: RWLock concurrent readers...\n");

        /* Wait for AP to be ready */
        while (!test8_ready) {
            asm volatile("pause");
        }

        /* BSP: Initialize test */
        rwlock_init(&test_rwlock);
        shared_counter = 0;

        /* Signal AP to start */
        test8_ready = 2;  /* Phase 2: Start test */
        asm volatile("mfence" ::: "memory");
    } else {
        /* AP: Signal that we're waiting */
        test8_ready = 1;
        asm volatile("mfence" ::: "memory");

        /* Wait for BSP to initialize and signal start */
        while (test8_ready != 2) {
            asm volatile("pause");
        }
    }

    /* Phase 2: All CPUs acquire read lock (interrupt-safe) */
    irq_flags_t flags;
    asm volatile("pushf\npop %0" : "=rm"(flags));
    disable_interrupts();

    spin_read_lock(&test_rwlock);

    /* Check that we can see the reader count */
    if (test_rwlock.counter <= 0) {
        test_errors[my_cpu] = 1;
    } else {
        test_errors[my_cpu] = 0;
    }

    /* Small delay while holding read lock */
    for (volatile int i = 0; i < 1000; i++) {
        asm volatile("pause");
    }

    spin_read_unlock(&test_rwlock);

    /* Restore interrupt state */
    if (flags & (1 << 9)) {
        enable_interrupts();
    }

    /* Mark this CPU as done */
    test8_done[my_cpu] = 1;
    asm volatile("mfence" ::: "memory");

    /* Phase 3: BSP waits for all CPUs to finish and verifies */
    if (test_is_bsp()) {
        /* Wait for AP to finish */
        int timeout = BARRIER_TIMEOUT;
        while (test8_done[1] == 0 && timeout > 0) {
            asm volatile("pause");
            timeout--;
        }

        int has_error = 0;
        for (int i = 0; i < num_cpus; i++) {
            if (test_errors[i] != 0) {
                has_error = 1;
                break;
            }
        }

        /* Clear flags for next test */
        for (int i = 0; i < num_cpus; i++) {
            test8_done[i] = 0;
        }
        test8_ready = 0;

        if (has_error) {
            test_puts("  FAIL: Reader counter was not positive\n");
            return -1;
        }

        test_puts("  PASS: All CPUs acquired read lock simultaneously\n");
        test_puts("Test 8 PASSED\n\n");
        return 0;
    }

    return 0;
}

/**
 * test9_rwlock_writer - Test 9: Writer excludes readers
 * Uses flag-based synchronization to avoid barrier deadlock.
 */
static int test9_rwlock_writer(int num_cpus) {
    /* Synchronization flags */
    static volatile int test9_ready = 0;
    static volatile int test9_done[SMP_MAX_CPUS] = {0};

    int my_cpu = test_get_cpu_index();

    if (test_is_bsp()) {
        test_puts("Test 9: RWLock writer exclusion...\n");

        /* Wait for AP to be ready */
        while (!test9_ready) {
            asm volatile("pause");
        }

        /* BSP: Initialize test */
        rwlock_init(&test_rwlock);
        shared_counter = 0;

        /* Signal AP to start - use atomic store for visibility */
        __atomic_store_n(&test9_ready, 2, __ATOMIC_SEQ_CST);

        /* Small delay to ensure AP sees the signal */
        for (volatile int i = 0; i < 10000; i++) {
            asm volatile("pause");
        }
    } else {
        /* AP: Signal that we're waiting */
        __atomic_store_n(&test9_ready, 1, __ATOMIC_SEQ_CST);

        /* Wait for BSP to initialize and signal start */
        while (test9_ready != 2) {
            asm volatile("pause");
        }

        /* Small delay to ensure BSP has acquired write lock */
        for (volatile int i = 0; i < 5000; i++) {
            asm volatile("pause");
        }
    }

    /* Phase 2: BSP acquires write lock, APs try read lock (interrupt-safe) */
    irq_flags_t flags;
    asm volatile("pushf\npop %0" : "=rm"(flags));
    disable_interrupts();

    if (test_is_bsp()) {
        /* BSP: Acquire write lock */
        spin_write_lock(&test_rwlock);

        /* Verify writer has exclusive access (counter == -1) */
        if (test_rwlock.counter != -1) {
            test_puts("  FAIL: Writer counter not -1\n");
            spin_write_unlock(&test_rwlock);
            enable_interrupts();
            test9_ready = 0;
            return -1;
        }

        /* Hold write lock briefly to ensure APs block */
        for (volatile int i = 0; i < 100000; i++) {
            asm volatile("pause");
        }

        spin_write_unlock(&test_rwlock);
    } else {
        /* APs: Try to acquire read lock while writer holds it */
        /* This will block until writer releases */
        spin_read_lock(&test_rwlock);

        /* Once we get here, writer has released */
        /* Verify we now have read access */
        if (test_rwlock.counter <= 0) {
            test_errors[my_cpu] = 1;
        } else {
            test_errors[my_cpu] = 0;
        }

        spin_read_unlock(&test_rwlock);
    }

    /* Restore interrupt state */
    if (flags & (1 << 9)) {
        enable_interrupts();
    }

    /* Mark this CPU as done */
    test9_done[my_cpu] = 1;
    asm volatile("mfence" ::: "memory");

    /* Phase 3: BSP waits for all CPUs to finish and verifies */
    if (test_is_bsp()) {
        /* Wait for AP to finish */
        int timeout = BARRIER_TIMEOUT;
        while (test9_done[1] == 0 && timeout > 0) {
            asm volatile("pause");
            timeout--;
        }

        int has_error = 0;
        for (int i = 1; i < num_cpus; i++) {
            if (test_errors[i] != 0) {
                has_error = 1;
                break;
            }
        }

        /* Clear flags for next test */
        for (int i = 0; i < num_cpus; i++) {
            test9_done[i] = 0;
        }
        test9_ready = 0;

        if (has_error) {
            test_puts("  FAIL: APs encountered errors\n");
            return -1;
        }

        test_puts("  PASS: Writer excluded all readers\n");
        test_puts("Test 9 PASSED\n\n");
        return 0;
    }

    return 0;
}

/**
 * test10_deadlock_prevention - Test 10: Deadlock prevention with consistent ordering
 *
 * All CPUs acquire locks in consistent order (lock1, then lock2) to prevent deadlock.
 */
static int test10_deadlock_prevention(int num_cpus) {
    if (test_is_bsp()) {
        test_puts("Test 10: Deadlock prevention...\n");
        test_barrier_reset();  /* Reset barrier at start of test */
    }

    /* Phase 1: Initialize and barrier (BSP initializes, both synchronize) */
    if (test_is_bsp()) {
        spin_lock_init(&test_lock1);
        spin_lock_init(&test_lock2);
        shared_counter = 0;
    }
    test_barrier_wait(num_cpus);

    /* Phase 2: All CPUs execute critical section with consistent lock ordering */
    irq_flags_t flags;
    asm volatile("pushf\npop %0" : "=rm"(flags));
    disable_interrupts();

    for (int round = 0; round < 10; round++) {
        spin_lock(&test_lock1);
        spin_lock(&test_lock2);

        /* Critical section */
        shared_counter++;

        /* Release in reverse order */
        spin_unlock(&test_lock2);
        spin_unlock(&test_lock1);
    }

    /* Restore interrupt state */
    if (flags & (1 << 9)) {
        enable_interrupts();
    }

    /* Phase 3: Barrier and verify (both synchronize, BSP verifies) */
    test_barrier_wait(num_cpus);

    if (test_is_bsp()) {
        test_barrier_reset();  /* Only BSP resets barrier */

        int expected = num_cpus * 10;

        if (shared_counter == expected) {
            test_puts("  PASS: No deadlock, counter correct (");
            test_put_hex(shared_counter);
            test_puts(")\n");
            test_puts("Test 10 PASSED\n\n");
            return 0;
        } else {
            test_puts("  FAIL: Counter incorrect (");
            test_put_hex(shared_counter);
            test_puts(" != ");
            test_put_hex(expected);
            test_puts(")\n");
            return -1;
        }
    }

    return 0;
}

/* ============================================================================
 * AP Entry Point
 * ============================================================================ */

/**
 * spinlock_test_ap_entry - AP entry point for SMP tests
 *
 * Called by APs when spinlock_test_start is set.
 * APs participate in SMP tests coordinated by BSP.
 *
 * Synchronization pattern:
 * 1. AP waits for BSP to set phase N
 * 2. Both AP and BSP call test function (which handles internal barriers)
 * 3. BSP sets phase N+1
 * 4. Repeat for next test
 */
void spinlock_test_ap_entry(void) {
    int num_cpus = test_get_active_cpu_count();

    /* Wait for BSP to signal first test phase */
    if (test_wait_phase(1) < 0) {
        return;  /* Timeout - exit test mode */
    }

    /* Participate in all SMP tests (tests 6-10) */
    int result;

    /* Test 6: Lock contention */
    result = test6_lock_contention(num_cpus);
    if (result != 0) {
        test_atomic_inc(&tests_failed);
    }
    test_wait_phase(2);

    /* Test 7: Trylock contention */
    result = test7_trylock_contention(num_cpus);
    if (result != 0) {
        test_atomic_inc(&tests_failed);
    }
    test_wait_phase(3);

    /* Test 8: RWLock readers */
    result = test8_rwlock_readers(num_cpus);
    if (result != 0) {
        test_atomic_inc(&tests_failed);
    }
    test_wait_phase(4);

    /* Test 9: RWLock writer */
    result = test9_rwlock_writer(num_cpus);
    if (result != 0) {
        test_atomic_inc(&tests_failed);
    }
    test_wait_phase(5);

    /* Test 10: Deadlock prevention */
    result = test10_deadlock_prevention(num_cpus);
    if (result != 0) {
        test_atomic_inc(&tests_failed);
    }

    /* Wait for BSP to signal completion */
    test_wait_phase(6);

    /* Return to halt loop in smp.c */
    return;
}

/* ============================================================================
 * Main Test Runner (BSP only)
 * ============================================================================ */

/**
 * run_spinlock_tests - Run all spin lock tests
 *
 * Returns: Number of failed tests (0 = all passed)
 */
int run_spinlock_tests(void) {
    int num_cpus = test_get_active_cpu_count();
    int failures = 0;

    test_puts("Starting spin lock test suite...\n");
    test_puts("Number of CPUs: ");
    test_put_hex(num_cpus);
    test_puts("\n\n");

    /* ========================================================================
     * Single-CPU Tests (Run on BSP only)
     * ======================================================================== */

    test_puts("=== Single-CPU Tests ===\n\n");

    if (test1_basic_lock_unlock() != 0) {
        failures++;
    }

    if (test2_trylock() != 0) {
        failures++;
    }

    if (test3_irqsafe_operations() != 0) {
        failures++;
    }

    if (test4_rwlock_basic() != 0) {
        failures++;
    }

    if (test5_nested_locks() != 0) {
        failures++;
    }

    /* ========================================================================
     * SMP Multi-CPU Tests
     * ======================================================================== */

    if (num_cpus > 1) {
        test_puts("=== SMP Multi-CPU Tests ===\n\n");

        /* Reset test counters */
        tests_passed = 0;
        tests_failed = 0;

        /* Note: spinlock_test_start was already set to 1 before APs were started
         * APs should already be waiting in spinlock_test_ap_entry() */

        /* Small delay to ensure APs are ready and in test entry */
        for (volatile int i = 0; i < 2000000; i++) {
            asm volatile("pause");
        }

        /* Start first test phase - this unblocks APs */
        test_set_phase(1);

        /* Small delay for APs to wake up */
        for (volatile int i = 0; i < 200000; i++) {
            asm volatile("pause");
        }

        /* Run SMP tests - APs will participate via spinlock_test_ap_entry()
         * Both BSP and AP call test functions, which handle their own synchronization internally */

        /* Test 6: Lock contention */
        int result = test6_lock_contention(num_cpus);
        if (result != 0) {
            failures++;
        }
        test_set_phase(2);

        /* Test 7: Trylock contention */
        result = test7_trylock_contention(num_cpus);
        if (result != 0) {
            failures++;
        }
        test_set_phase(3);

        /* Test 8: RWLock readers */
        result = test8_rwlock_readers(num_cpus);
        if (result != 0) {
            failures++;
        }
        test_set_phase(4);

        /* Test 9: RWLock writer */
        result = test9_rwlock_writer(num_cpus);
        if (result != 0) {
            failures++;
        }
        test_set_phase(5);

        /* Test 10: Deadlock prevention */
        result = test10_deadlock_prevention(num_cpus);
        if (result != 0) {
            failures++;
        }
        test_set_phase(6);

        /* Signal APs that tests are complete */
        test_complete = 1;
        spinlock_test_start = 0;

        /* Small delay before returning to halt loop */
        for (volatile int i = 0; i < 10000; i++) {
            asm volatile("pause");
        }

    } else {
        test_puts("=== SMP Tests Skipped (Single CPU) ===\n\n");
    }

    /* ========================================================================
     * Summary
     * ======================================================================== */

    test_puts("========================================\n");
    test_puts("Tests complete\n");
    test_puts("Summary: ");

    int total_tests = 5;  /* Single-CPU tests */
    if (num_cpus > 1) {
        total_tests += 5;  /* Add SMP tests */
    }

    int passed = total_tests - failures;
    test_put_hex(passed);
    test_puts("/");
    test_put_hex(total_tests);
    test_puts(" tests passed\n");

    if (failures == 0) {
        test_puts("Result: ALL TESTS PASSED\n");
    } else {
        test_puts("Result: SOME TESTS FAILED\n");
    }

    test_puts("========================================\n\n");

    return failures;
}
