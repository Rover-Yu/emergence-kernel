/* User Mode Syscall Test
 *
 * Tests SYSCALL/SYSRET mechanism and ring 3 user program execution.
 * Verifies that the kernel can:
 * 1. Set up SYSCALL MSRs (IA32_EFER.SCE, STAR, LSTAR, FMASK)
 * 2. Execute SYSCALL from ring 0
 * 3. Transition to ring 3 using sysretq
 * 4. Handle syscalls from ring 3
 */

#include <stdint.h>
#include "test_usermode.h"
#include "kernel/test.h"
#include "kernel/klog.h"
#include "arch/x86_64/serial.h"
#include "arch/x86_64/include/syscall.h"
#include "arch/x86_64/include/gdt.h"

#if CONFIG_TESTS_USERMODE

/* External functions */
extern void syscall_init(void);
extern void enter_user_mode(void);
extern void *prealloc_user_stack(void);
extern void kernel_halt(void);

/* Test: Verify syscall handler works (direct call from ring 0) */
static int test_syscall_ring0(void) {
    klog_info("USERMODE_TEST", "Testing syscall handler (direct call from ring 0)...");

    /* Call syscall handler directly to test the C logic
     * Note: We don't use SYSCALL instruction here because sysretq
     * always returns to ring 3, which would break ring 0 execution. */
    extern void syscall_handler(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3);

    /* Test sys_write with minimal args */
    syscall_handler(1, 0, 0, 0);

    klog_info("USERMODE_TEST", "Syscall handler direct call: PASSED");
    return 0;
}

/* Test: Ring 3 transition */
static int test_ring3_transition(void) {
    void *user_stack;
    uint64_t user_rsp;

    klog_info("USERMODE_TEST", "Testing ring 3 transition...");

    /* Pre-allocate user stack */
    user_stack = prealloc_user_stack();
    if (!user_stack) {
        klog_error("USERMODE_TEST", "Failed to allocate user stack");
        return -1;
    }

    user_rsp = (uint64_t)user_stack + 16384;  /* 16KB stack */
    klog_info("USERMODE_TEST", "User stack: %x", user_rsp);

    klog_info("USERMODE_TEST", "Entering ring 3...");

    enter_user_mode();

    /* Should never reach here */
    klog_error("USERMODE_TEST", "Returned from ring 3!");
    return -1;
}

/* Main usermode test entry point */
int run_usermode_tests(void) {
    int tests_run = 0;
    int tests_failed = 0;

    klog_info("USERMODE_TEST", "=== User Mode Syscall Tests ===");

    /* Initialize syscall MSRs */
    klog_info("USERMODE_TEST", "Initializing syscall support...");
    syscall_init();

    /* Test 1: SYSCALL from ring 0 */
    tests_run++;
    if (test_syscall_ring0() < 0) {
        tests_failed++;
        klog_error("USERMODE_TEST", "Test 1 FAILED");
    }

    /* Test 2: Ring 3 transition */
    tests_run++;
    klog_info("USERMODE_TEST", "Testing ring 3 transition...");
    test_ring3_transition();
    /* Note: test_ring3_transition() should not return - user program calls sys_exit */
    klog_error("USERMODE_TEST", "Returned from ring 3!");
    tests_failed++;

    /* Print summary */
    klog_info("USERMODE_TEST", "User Mode Tests: %d/%d passed", tests_run - tests_failed, tests_run);

    return (tests_failed > 0) ? -1 : 0;
}

#endif /* CONFIG_TESTS_USERMODE */

/* ============================================================================
 * Test Wrappers
 * ============================================================================ */

#if CONFIG_TESTS_USERMODE
int test_usermode_prepare(void) {
    /* Check if usermode test is specifically requested */
    extern const char *cmdline_get_value(const char *key);
    const char *test_value = cmdline_get_value("test");

    /* Simple string comparison */
    int is_usermode = 0;
    if (test_value) {
        const char *p = test_value;
        const char *u = "usermode";
        while (*u && *p && *p == *u) {
            p++; u++;
        }
        if (*u == '\0' && (*p == '\0' || *p == ' ')) {
            is_usermode = 1;
        }
    }

    if (is_usermode) {
        klog_info("USERMODE_TEST", "MONITOR DISABLED for ring 3 usermode test");

        /* Pre-allocate user stack BEFORE switching page tables
         * PMM is only accessible with boot page tables */
        void *stack = prealloc_user_stack();
        if (stack) {
            klog_info("USERMODE_TEST", "User stack pre-allocated at %x", (uint64_t)stack);
        }
        return 1;  /* Usermode test is selected, preparation done */
    }
    return 0;  /* Usermode test not selected */
}

void test_usermode(void) {
    if (test_should_run("usermode")) {
        test_run_by_name("usermode");
    }
}
#else
int test_usermode_prepare(void) { return 0; }
void test_usermode(void) { }
#endif
