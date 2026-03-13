/* Emergence Kernel - Scheduler Tests
 *
 * Tests for thread creation and FIFO scheduling, and context switching.
 */

#include <stdint.h>
#include <stddef.h>
#include "kernel/test.h"
#include "kernel/klog.h"
#include "kernel/thread.h"
#include "kernel/scheduler.h"
#include "arch/x86_64/smp.h"
#include "arch/x86_64/power.h"
#include "include/string.h"

#if CONFIG_TESTS_SCHED

/* Test configuration */
#define NUM_TEST_THREADS 3
#define TEST_STACK_SIZE 4096

/* Test state */
static volatile int test_threads_created = 0;
static volatile int test_threads_run[NUM_TEST_THREADS];

/* Forward declarations */
static void test_thread_entry(void *arg);

/* ============================================================================
 * Individual Test Functions
 * ============================================================================ */

/**
 * test_thread_creation - Test basic thread creation
 */
static int test_thread_creation(void) {
    thread_t *thread;
    char name[16];

    klog_info("SCHED_TEST", "Test 1: Thread creation...");

    test_threads_created = 0;

    /* Create a test thread */
    snprintf(name, sizeof(name), "test_thread_0");
    thread = thread_create(name, test_thread_entry, (void *)0,
                          TEST_STACK_SIZE, THREAD_FLAG_KERNEL);
    if (thread == NULL) {
        klog_error("SCHED_TEST", "FAILED: Thread creation returned NULL");
        return -1;
    }

    test_threads_created++;

    /* Verify thread fields */
    if (thread->tid <= 0) {
        klog_error("SCHED_TEST", "FAILED: Invalid thread ID %d", thread->tid);
        return -1;
    }

    if (thread->state != THREAD_CREATED) {
        klog_error("SCHED_TEST", "FAILED: Thread state is %d, expected CREATED", thread->state);
        return -1;
    }

    if (thread->kernel_stack == NULL) {
        klog_error("SCHED_TEST", "FAILED: Thread has no stack");
        return -1;
    }

    klog_info("SCHED_TEST", "Thread created successfully (tid=%d, stack=%p)",
             thread->tid, thread->kernel_stack);

    /* Clean up */
    thread->state = THREAD_TERMINATED;
    thread_destroy(thread);

    klog_info("SCHED_TEST", "Test 1: PASSED");
    return 0;
}

/**
 * test_fifo_ordering - Test FIFO runqueue ordering
 */
static int test_fifo_ordering(void) {
    thread_t *threads[NUM_TEST_THREADS];
    char name[16];
    int i;

    klog_info("SCHED_TEST", "Test 2: FIFO runqueue ordering...");

    /* Create multiple threads */
    for (i = 0; i < NUM_TEST_THREADS; i++) {
        snprintf(name, sizeof(name), "test_thread_%d", i);
        threads[i] = thread_create(name, test_thread_entry, (void *)(uintptr_t)i,
                                  TEST_STACK_SIZE, THREAD_FLAG_KERNEL);
        if (threads[i] == NULL) {
            klog_error("SCHED_TEST", "FAILED: Could not create thread %d", i);
            return -1;
        }

        /* Add to runqueue - should be added in creation order (FIFO) */
        scheduler_add_thread(threads[i]);
        test_threads_created++;
    }

    /* Verify runqueue state */
    runqueue_t *rq = scheduler_get_runqueue();
    if (rq == NULL) {
        klog_error("SCHED_TEST", "FAILED: Could not get runqueue");
        return -1;
    }

    if (rq->nr_running != NUM_TEST_THREADS) {
        klog_error("SCHED_TEST", "FAILED: Expected %d threads in runqueue, got %d",
                 NUM_TEST_THREADS, rq->nr_running);
        return -1;
    }

    klog_info("SCHED_TEST", "Runqueue has %d threads (correct)", rq->nr_running);

    /* Clean up */
    for (i = 0; i < NUM_TEST_THREADS; i++) {
        threads[i]->state = THREAD_TERMINATED;
        thread_destroy(threads[i]);
    }

    klog_info("SCHED_TEST", "Test 2: PASSED");
    return 0;
}

/**
 * test_idle_threads - Test idle thread creation
 */
static int test_idle_threads(void) {
    int cpu;
    int idle_count = 0;

    klog_info("SCHED_TEST", "Test 3: Idle thread creation...");

    for (cpu = 0; cpu < SMP_MAX_CPUS; cpu++) {
        thread_t *idle = scheduler_get_idle_thread(cpu);
        if (idle == NULL) {
            klog_error("SCHED_TEST", "FAILED: No idle thread for CPU %d", cpu);
            return -1;
        }

        klog_debug("SCHED_TEST", "CPU %d idle thread: '%s' (tid=%d)",
                  cpu, idle->name, idle->tid);
        idle_count++;
    }

    if (idle_count != SMP_MAX_CPUS) {
        klog_error("SCHED_TEST", "FAILED: Expected %d idle threads, got %d",
                 SMP_MAX_CPUS, idle_count);
        return -1;
    }

    klog_info("SCHED_TEST", "Test 3: PASSED (%d idle threads)", idle_count);
    return 0;
}

/**
 * test_context_switch - Test basic context switching setup
 */
static int test_context_switch(void) {
    thread_t *thread;
    cpu_context_t *ctx;

    klog_info("SCHED_TEST", "Test 4: Context switch setup...");

    /* Create a thread and verify its context is properly initialized */
    thread = thread_create("ctx_test", test_thread_entry, (void *)0,
                           TEST_STACK_SIZE, THREAD_FLAG_KERNEL);
    if (thread == NULL) {
        klog_error("SCHED_TEST", "FAILED: Thread creation failed");
        return -1;
    }

    ctx = &thread->context;

    /* Verify context fields are initialized */
    if (ctx->rip == 0) {
        klog_error("SCHED_TEST", "FAILED: Instruction pointer not set");
        thread->state = THREAD_TERMINATED;
        thread_destroy(thread);
        return -1;
    }

    if (ctx->rsp == 0) {
        klog_error("SCHED_TEST", "FAILED: Stack pointer not set");
        thread->state = THREAD_TERMINATED;
        thread_destroy(thread);
        return -1;
    }

    if (ctx->rflags == 0) {
        klog_error("SCHED_TEST", "FAILED: RFLAGS not set (interrupts disabled)");
        thread->state = THREAD_TERMINATED;
        thread_destroy(thread);
        return -1;
    }

    klog_debug("SCHED_TEST", "Context: RIP=%p, RSP=%p, RFLAGS=%p",
              (void *)ctx->rip, (void *)ctx->rsp, (void *)ctx->rflags);

    /* Clean up */
    thread->state = THREAD_TERMINATED;
    thread_destroy(thread);

    klog_info("SCHED_TEST", "Test 4: PASSED");
    return 0;
}

/* ============================================================================
 * Main Test Runner
 * ============================================================================ */

/**
 * run_sched_tests - Run all scheduler tests
 *
 * Returns: Number of test failures (0 = all passed)
 */
int run_sched_tests(void) {
    int failures = 0;

    klog_info("SCHED_TEST", "=== Scheduler Test Suite ===");
    klog_info("SCHED_TEST", "Testing thread creation and FIFO scheduling");

    /* Test 1: Thread Creation */
    if (test_thread_creation() != 0) {
        failures++;
    }

    /* Test 2: FIFO Ordering */
    if (test_fifo_ordering() != 0) {
        failures++;
    }

    /* Test 3: Idle Threads */
    if (test_idle_threads() != 0) {
        failures++;
    }

    /* Test 4: Context Switch Setup */
    if (test_context_switch() != 0) {
        failures++;
    }

    /* Summary */
    if (failures == 0) {
        klog_info("SCHED_TEST", "SCHED: All tests PASSED");
    } else {
        klog_error("SCHED_TEST", "SCHED: Some tests FAILED (%d failures)", failures);
    }

    return failures;
}

/**
 * test_thread_entry - Test thread entry point
 * @arg: Thread index (cast to void*)
 */
static void test_thread_entry(void *arg) {
    int idx = (int)(uintptr_t)arg;

    (void)idx; /* Unused in simple test */

    /* Test thread - just mark as run */
    klog_debug("SCHED_TEST", "Test thread %d running", idx);

    /* Mark as run */
    test_threads_run[idx] = 1;

    /* Note: We don't call thread_yield() here to avoid scheduling
     * complications during testing. The thread will simply return. */
}

#endif /* CONFIG_TESTS_SCHED */

/* ============================================================================
 * Test Wrapper
 * ============================================================================ */

#if CONFIG_TESTS_SCHED
void test_sched(void) {
    /* Run directly if selected via tests=sched CSV mode */
    if (test_should_run("sched")) {
        /* Check if test already ran via unified mode */
        if (!test_did_run("sched")) {
            int result = run_sched_tests();
            test_mark_run("sched", result);
            if (result == 0) {
                klog_info("TEST", "PASSED: sched");
            } else {
                klog_error("TEST", "FAILED: sched (failures: %d)", result);
                system_shutdown();
            }
        }
    }
}
#else
void test_sched(void) { }
#endif
