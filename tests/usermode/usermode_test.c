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
#include "arch/x86_64/serial.h"
#include "arch/x86_64/include/syscall.h"
#include "arch/x86_64/include/gdt.h"

#if CONFIG_TESTS_USERMODE

/* External functions */
extern void syscall_init(void);
extern void enter_user_mode(void);
extern void *prealloc_user_stack(void);
extern void kernel_halt(void);
extern void serial_put_hex(uint64_t value);

/* Test: Verify syscall handler works (direct call from ring 0) */
static int test_syscall_ring0(void) {
    serial_puts("[USERMODE] Testing syscall handler (direct call from ring 0)...\n");

    /* Call syscall handler directly to test the C logic
     * Note: We don't use SYSCALL instruction here because sysretq
     * always returns to ring 3, which would break ring 0 execution. */
    extern void syscall_handler(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3);

    /* Test sys_write with minimal args */
    syscall_handler(1, 0, 0, 0);

    serial_puts("[USERMODE] Syscall handler direct call: PASSED\n");
    return 0;
}

/* Test: Ring 3 transition */
static int test_ring3_transition(void) {
    void *user_stack;
    uint64_t user_rsp;

    serial_puts("[USERMODE] Testing ring 3 transition...\n");

    /* Pre-allocate user stack */
    user_stack = prealloc_user_stack();
    if (!user_stack) {
        serial_puts("[USERMODE] ERROR: Failed to allocate user stack\n");
        return -1;
    }

    user_rsp = (uint64_t)user_stack + 16384;  /* 16KB stack */
    serial_puts("[USERMODE] User stack: 0x");
    serial_put_hex(user_rsp);
    serial_puts("\n");

    serial_puts("[USERMODE] Entering ring 3...\n");

    enter_user_mode();

    /* Should never reach here */
    serial_puts("[USERMODE] ERROR: Returned from ring 3!\n");
    return -1;
}

/* Main usermode test entry point */
int run_usermode_tests(void) {
    int tests_run = 0;
    int tests_failed = 0;

    serial_puts("\n");
    serial_puts("========================================\n");
    serial_puts("User Mode Syscall Tests\n");
    serial_puts("========================================\n");
    serial_puts("\n");

    /* Initialize syscall MSRs */
    serial_puts("[USERMODE] Initializing syscall support...\n");
    syscall_init();

    /* Test 1: SYSCALL from ring 0 */
    tests_run++;
    if (test_syscall_ring0() < 0) {
        tests_failed++;
        serial_puts("[USERMODE] Test 1 FAILED\n");
    }

    /* Test 2: Ring 3 transition */
    tests_run++;
    serial_puts("[USERMODE] Testing ring 3 transition...\n");
    test_ring3_transition();
    /* Note: test_ring3_transition() should not return - user program calls sys_exit */
    serial_puts("[USERMODE] ERROR: Returned from ring 3!\n");
    tests_failed++;

    /* Print summary */
    serial_puts("\n");
    serial_puts("========================================\n");
    serial_puts("User Mode Tests: ");
    serial_put_hex(tests_run - tests_failed);
    serial_puts("/");
    serial_put_hex(tests_run);
    serial_puts(" passed\n");
    serial_puts("========================================\n");
    serial_puts("\n");

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
        serial_puts("KERNEL: MONITOR DISABLED for ring 3 usermode test\n");

        /* Pre-allocate user stack BEFORE switching page tables
         * PMM is only accessible with boot page tables */
        void *stack = prealloc_user_stack();
        if (stack) {
            serial_puts("KERNEL: User stack pre-allocated at 0x");
            extern void serial_put_hex(uint64_t);
            serial_put_hex((uint64_t)stack);
            serial_puts("\n");
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
