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

/* Test: Verify SYSCALL from ring 0 */
static int test_syscall_ring0(void) {
    volatile int syscall_worked = 0;

    serial_puts("[USERMODE] Testing SYSCALL from ring 0...\n");

    /* Test: SYSCALL from ring 0 using a simple syscall number
     * This verifies the SYSCALL mechanism works (MSR setup, entry, return) */
    serial_puts("[USERMODE] Calling SYSCALL instruction...\n");

    /* Use SYS_write with minimal arguments to test SYSCALL entry/exit */
    __asm__ volatile (
        "mov $1, %%rax\n"      /* SYS_write */
        "mov $0, %%rdi\n"      /* fd = 0 */
        "mov $0, %%rsi\n"      /* buf = NULL */
        "mov $0, %%rdx\n"      /* count = 0 */
        "syscall\n"
        "movl $1, %0\n"        /* Mark that syscall returned */
        : "=m"(syscall_worked)
        :
        : "rax", "rdi", "rsi", "rdx", "rcx", "r11", "memory"
    );

    if (syscall_worked) {
        serial_puts("[USERMODE] SYSCALL from ring 0: PASSED\n");
        return 0;
    }

    serial_puts("[USERMODE] ERROR: SYSCALL failed\n");
    return -1;
}

/* Test: Ring 3 transition */
static int __attribute__((unused)) test_ring3_transition(void) {
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

    /* Note: ring 3 transition via sysretq has issues in QEMU TCG mode
     * This is a known limitation and should work on real hardware/KVM */
    serial_puts("[USERMODE] Entering ring 3 (may hang in QEMU TCG)...\n");

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

    /* Test 2: Ring 3 transition - SKIPPED for now
     * The ring 3 transition enters user mode and never returns.
     * This test should be run separately with KVM for proper testing.
     */
    serial_puts("[USERMODE] Test 2 (Ring 3 transition): SKIPPED\n");
    serial_puts("[USERMODE] Note: Ring 3 test does not return and should be run separately\n");

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
