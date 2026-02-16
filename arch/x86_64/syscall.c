#include <stdint.h>
#include <stddef.h>
#include "syscall.h"
#include "arch/x86_64/include/gdt.h"
#include "serial.h"
#include "kernel/monitor/monitor.h"  /* For g_unpriv_pd_ptr */
#include "include/barrier.h"
#include "kernel/klog.h"

/* External: kernel stack top from boot.S */
extern uint64_t nk_boot_stack_top;

/* External: halt CPU from main.c */
extern void kernel_halt(void);

/* MSR addresses */
#define MSR_IA32_STAR        0xC0000081
#define MSR_IA32_LSTAR       0xC0000082
#define MSR_IA32_CSTAR       0xC0000083
#define MSR_IA32_FMASK       0xC0000084
#define MSR_IA32_EFER        0xC0000080

/* External syscall entry assembly function */
extern void syscall_entry(void);

/* External jump_to_user_mode assembly function */
extern void jump_to_user_mode(uint64_t user_rip, uint64_t user_rsp, uint64_t user_rflags);

/* Debug function to print sysretq parameters */
void debug_sysret_params(uint64_t rip, uint64_t rsp, uint64_t rflags) {
    klog_debug("SYSCALL", "sysretq params: RIP=%p RSP=%p RFLAGS=%p", rip, rsp, rflags);
}

/* External user program entry */
extern void user_program_start(void);

/* User program entry - must be executed in ring 3 */
void inline_user_program(void) {
    /* Simple test: make a syscall to verify ring 3 execution */
    volatile int syscall_worked = 0;

    /* Make a write syscall to verify we can call into kernel from ring 3 */
    const char *msg = "[USER] Ring 3 user program running!";
    __asm__ volatile (
        "mov $1, %%rax\n"        /* SYS_write */
        "mov $1, %%rdi\n"        /* fd = stdout */
        "mov %1, %%rsi\n"        /* buf */
        "mov $0, %%rdx\n"        /* count = 0 */
        "syscall\n"
        "movl $1, %0\n"          /* Mark success */
        : "=m"(syscall_worked)
        : "r"(msg)
        : "rax", "rdi", "rsi", "rdx", "rcx", "r11", "memory", "cc"
    );

    /* Infinite loop after successful syscall */
    while (1) {
        cpu_relax();
    }
}

/* External PMM for allocating user stack */
extern void *pmm_alloc(int order);

/* User program stack - pre-allocated before page table switch */
static uint8_t *user_stack = NULL;
static uint64_t user_stack_size = 16384;

/* Pre-allocate user stack before switching to unprivileged page tables
 * This is called from main.c before CR3 switch */
void *prealloc_user_stack(void) {
    if (user_stack == NULL) {
        user_stack = pmm_alloc(2);  /* Allocate 4 pages (16KB) */
        if (user_stack != 0) {
            /* Clear the stack for cleanliness */
            for (uint64_t i = 0; i < user_stack_size; i++) {
                user_stack[i] = 0;
            }
        }
    }
    return user_stack;
}

/* Syscall implementations */
static int64_t sys_write(uint64_t fd, const char *buf, uint64_t count) {
    (void)fd;  /* Only stdout for now */
    if (buf == 0) return -1;

    /* Simple output - don't dereference user pointer yet (no user paging) */
    klog_info("USER", "%s", buf);

    return count;
}

static void sys_exit(int64_t exit_code) {
    klog_info("USER", "Process exited with code: %d", (int)exit_code);
    kernel_halt();  /* Halt for now - no scheduler yet */
}

/* Syscall dispatcher */
void syscall_handler(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3) {
    /* Debug: syscall was called! */
    klog_debug("SYSCALL", "Syscall %x args: %x %x %x", nr, a1, a2, a3);

    int64_t result;

    switch (nr) {
        case 1: /* SYS_write */
            result = sys_write(a1, (const char *)a2, a3);
            /* Return value in RAX */
            __asm__ volatile ("mov %0, %%rax" : : "r"(result) : "rax");
            klog_debug("SYSCALL", "Write returned: %d", (int)result);
            break;
        case 2: /* SYS_exit */
            sys_exit(a1);
            break;
        default:
            klog_warn("SYSCALL", "Unknown syscall: %x", nr);
            __asm__ volatile ("mov $-1, %%rax" ::: "rax");
            break;
    }
}

/* Initialize SYSCALL MSRs */
void syscall_init(void) {
    uint64_t star, lstar, fmask, efer;

    klog_info("SYSCALL", "syscall_init: starting...");

    /* CRITICAL: Enable SYSCALL/SYSRET by setting SCE bit in IA32_EFER */
    __asm__ volatile (
        "rdmsr"
        : "=a"(efer), "=d"(star)  /* Use star as temp for high dword */
        : "c"(MSR_IA32_EFER)
    );
    efer |= (1 << 0);  /* Set SCE (System Call Extension) bit */
    __asm__ volatile (
        "wrmsr"
        : : "c"(MSR_IA32_EFER), "a"(efer & 0xFFFFFFFF), "d"(efer >> 32)
    );
    klog_info("SYSCALL", "SYSCALL/SYSRET enabled (IA32_EFER.SCE=1)");

    /* STAR: [63:48] = kernel CS, [47:32] = user CS for SYSRET
     * STAR format:
     *   [63:48] = kernel CS (for SYSCALL target)
     *   [47:32] = user CS (for SYSRET target)
     *
     * For our GDT:
     *   Kernel CS = 0x08 (selector, index 1)
     *   User CS = 0x18 (selector, index 3)
     *
     * STAR = (kernel_CS << 48) | (user_CS << 32)
     *      = (0x08 << 48) | (0x18 << 32)
     *      = 0x0008000180000000
     */
    /* STAR: [63:48] = kernel CS (0x08), [47:32] = user CS (0x18)
     * Format: 0x0008_0018_0000_0000
     *
     * Split for wrmsr (EAX=low 32, EDX=high 32):
     * - EAX = 0x00000000
     * - EDX = 0x00080018
     */
    uint32_t star_low = 0x00000000;                    /* STAR[31:0] */
    uint32_t star_high = 0x00080018;                   /* STAR[63:32] */

    klog_debug("SYSCALL", "Computed STAR: %p", ((uint64_t)star_high << 32) | star_low);

    /* LSTAR: Entry point for SYSCALL */
    lstar = (uint64_t)syscall_entry;

    /* FMASK: Clear IF on syscall */
    fmask = 0x200;

    klog_debug("SYSCALL", "writing STAR MSR...");
    __asm__ volatile (
        "wrmsr"
        : : "c"(MSR_IA32_STAR), "a"(star_low), "d"(star_high)
    );

    /* Verify STAR was written correctly */
    uint32_t read_low, read_high;
    __asm__ volatile (
        "rdmsr"
        : "=a"(read_low), "=d"(read_high)
        : "c"(MSR_IA32_STAR)
    );
    klog_debug("SYSCALL", "STAR verify: EAX=%p EDX=%p", read_low, read_high);
    klog_debug("SYSCALL", "STAR[63:48] (kernel CS): %p", read_high >> 16);
    klog_debug("SYSCALL", "STAR[47:32] (user CS): %p", read_high & 0xFFFF);

    klog_debug("SYSCALL", "writing LSTAR MSR...");
    __asm__ volatile (
        "wrmsr"
        : : "c"(MSR_IA32_LSTAR), "a"(lstar & 0xFFFFFFFF), "d"(lstar >> 32)
    );

    klog_debug("SYSCALL", "writing FMASK MSR...");
    __asm__ volatile (
        "wrmsr"
        : : "c"(MSR_IA32_FMASK), "a"(fmask & 0xFFFFFFFF), "d"(fmask >> 32)
    );

    klog_info("SYSCALL", "Syscall initialized");
}

/* Jump to user mode - MINIMAL VERSION for debugging */
void enter_user_mode(void) {
    klog_info("SYSCALL", "Entering user mode (MINIMAL)...");

    /* Get address of user program */
    uint64_t user_rip = (uint64_t)user_program_start;
    uint64_t user_rsp = (uint64_t)user_stack + user_stack_size;

    klog_debug("SYSCALL", "user_program_start address: %p", user_rip);

    /* For iretq, we just need 16-byte stack alignment
     * RIP doesn't need special alignment */
    user_rsp &= ~0xF;

    klog_debug("SYSCALL", "User RIP: %p, RSP: %p", user_rip, user_rsp);

    /* RFLAGS: IF=1, bit 1=1 (reserved but must be 1) */
    uint64_t user_rflags = 0x202;

    /* Jump to user mode using the assembly function */
    klog_debug("SYSCALL", "Calling jump_to_user_mode...");
    jump_to_user_mode(user_rip, user_rsp, user_rflags);

    /* Should never reach here */
    klog_error("SYSCALL", "ERROR: Returned from jump_to_user_mode!");
}
