/* Syscall Tests - Tests for fork, getpid, yield, wait syscalls
 *
 * Tests the new syscalls implemented for process management:
 * - SYS_getpid: Get process ID
 * - SYS_yield: Voluntarily yield CPU
 * - SYS_fork: Create child process
 * - SYS_wait: Wait for child process
 */

#include <stdint.h>
#include "test_syscall.h"
#include "kernel/test.h"
#include "kernel/klog.h"
#include "arch/x86_64/include/syscall.h"

#if CONFIG_TESTS_SYSCALL

/* External functions */
extern void syscall_init(void);
extern void enter_syscall_test_mode(void);
extern void *prealloc_user_stack(void);
extern void kernel_halt(void);

/* Test: Verify syscall initialization */
static int test_syscall_init(void) {
    klog_info("SYSCALL_TEST", "Testing syscall initialization...");

    /* Initialize syscall MSRs */
    syscall_init();

    klog_info("SYSCALL_TEST", "Syscall initialization: PASSED");
    return 0;
}

/* Test: Ring 3 transition with syscall test program */
static int test_syscall_ring3_test(void) {
    void *user_stack;
    uint64_t user_rsp;

    klog_info("SYSCALL_TEST", "Testing ring 3 syscall test program...");

    /* Pre-allocate user stack */
    user_stack = prealloc_user_stack();
    if (!user_stack) {
        klog_error("SYSCALL_TEST", "Failed to allocate user stack");
        return -1;
    }

    user_rsp = (uint64_t)user_stack + 16384;  /* 16KB stack */
    klog_info("SYSCALL_TEST", "User stack: %x", user_rsp);

    klog_info("SYSCALL_TEST", "Entering ring 3 for syscall test...");

    /* Enter syscall test mode - runs user program that tests all new syscalls */
    enter_syscall_test_mode();

    /* Should never reach here */
    klog_error("SYSCALL_TEST", "Returned from syscall test!");
    return -1;
}

/* Main syscall test entry point */
int run_syscall_tests(void) {
    int tests_run = 0;
    int tests_failed = 0;

    klog_info("SYSCALL_TEST", "=== Syscall Tests ===");

    /* Test 1: Syscall initialization */
    tests_run++;
    if (test_syscall_init() < 0) {
        tests_failed++;
        klog_error("SYSCALL_TEST", "Test 1 FAILED");
    }

    /* Test 2: Ring 3 syscall test program */
    tests_run++;
    klog_info("SYSCALL_TEST", "Testing ring 3 syscall test program...");
    test_syscall_ring3_test();
    /* Note: test_syscall_ring3_test() should not return - user program calls sys_exit */
    klog_error("SYSCALL_TEST", "Returned from syscall test!");
    tests_failed++;

    /* Print summary */
    klog_info("SYSCALL_TEST", "Syscall Tests: %d/%d passed", tests_run - tests_failed, tests_run);

    return (tests_failed > 0) ? -1 : 0;
}

#endif /* CONFIG_TESTS_SYSCALL */

/* ============================================================================
 * Test Wrappers
 * ============================================================================ */

#if CONFIG_TESTS_SYSCALL
int test_syscall_prepare(void) {
    /* Check if syscall test is specifically requested */
    extern const char *cmdline_get_value(const char *key);
    const char *test_value = cmdline_get_value("test");

    /* Simple string comparison */
    int is_syscall = 0;
    if (test_value) {
        const char *p = test_value;
        const char *s = "syscall";
        while (*s && *p && *p == *s) {
            p++; s++;
        }
        if (*s == '\0' && (*p == '\0' || *p == ' ')) {
            is_syscall = 1;
        }
    }

    if (is_syscall) {
        klog_info("SYSCALL_TEST", "MONITOR DISABLED for ring 3 syscall test");

        /* Pre-allocate user stack BEFORE switching page tables */
        void *stack = prealloc_user_stack();
        if (stack) {
            klog_info("SYSCALL_TEST", "User stack pre-allocated at %x", (uint64_t)stack);
        }
        return 1;  /* Syscall test is selected, preparation done */
    }
    return 0;  /* Syscall test not selected */
}

void test_syscall(void) {
    if (test_should_run("syscall")) {
        test_run_by_name("syscall");
    }
}
#else
int test_syscall_prepare(void) { return 0; }
void test_syscall(void) { }
#endif
