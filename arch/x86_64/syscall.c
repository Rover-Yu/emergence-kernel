#include <stdint.h>
#include <stddef.h>
#include "syscall.h"
#include "arch/x86_64/include/gdt.h"
#include "arch/x86_64/include/uaccess.h"
#include "arch/x86_64/serial.h"
#include "arch/x86_64/power.h"
#include "kernel/monitor/monitor.h"  /* For g_unpriv_pd_ptr */
#include "include/barrier.h"
#include "kernel/klog.h"
#include "kernel/process.h"
#include "kernel/thread.h"
#include "kernel/scheduler.h"
#include "kernel/vm.h"
#include "kernel/pmm.h"
#include "arch/x86_64/smp.h"

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

/* External syscall test program entry */
extern void syscall_test_start(void);

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

/* User program stack - pre-allocated before page table switch */
static uint8_t *user_stack = NULL;
static uint64_t user_stack_size = 16384;

/* Per-CPU buffer for sys_write - eliminates SMP race condition */
#define SYS_WRITE_BUF_SIZE 4096
static char sys_write_buf[SMP_MAX_CPUS][SYS_WRITE_BUF_SIZE];

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

/**
 * sys_write - Write to a file descriptor
 * @fd: File descriptor (1 = stdout)
 * @buf: User buffer containing data to write
 * @count: Number of bytes to write
 *
 * Returns: Number of bytes written, or negative error code
 */
static int64_t sys_write(uint64_t fd, const char *buf, uint64_t count) {
    /* Per-CPU buffer eliminates SMP race condition from static buffer */
    int cpu;
    char *kernel_buf;
    long ret;

    /* Get per-CPU buffer */
    cpu = smp_get_cpu_index();
    kernel_buf = sys_write_buf[cpu];

    (void)fd;  /* Only stdout for now */

    klog_debug("SYSCALL", "sys_write: fd=%d, buf=%p, count=%d (CPU=%d)",
               (int)fd, buf, (int)count, cpu);

    /* Validate arguments */
    if (buf == 0) {
        klog_warn("SYSCALL", "sys_write: NULL buffer");
        return -1;  /* EFAULT */
    }

    if (count == 0) {
        return 0;  /* Nothing to write */
    }

    if (count > 4095) {
        count = 4095;  /* Limit write size to leave space for null terminator */
    }

    /* Copy data from user space with validation */
    ret = copy_from_user(kernel_buf, buf, count);
    if (ret != 0) {
        klog_warn("SYSCALL", "copy_from_user failed (err=%ld)", ret);
        return ret;
    }

    /* Null-terminate for safety (count is guaranteed < 4096) */
    kernel_buf[count] = '\0';

    /* Output to kernel log (stdout for now) */
    klog_info("USER", "%s", kernel_buf);

    return count;
}

/**
 * sys_exit - Terminate the current process
 * @exit_code: Exit status code
 *
 * Does not return.
 */
static void sys_exit(int64_t exit_code) {
    klog_info("SYSCALL", "sys_exit: code=%d", (int)exit_code);

    /* Check if we have a current process */
    process_t *p = process_get_current();
    if (p != NULL) {
        /* Check if this is the test process (no parent, PID=2)
         * In this case, shut down the system directly */
        if (p->parent == NULL || p->parent->pid == 1) {
            klog_info("USER", "Test process exited with code: %d", (int)exit_code);

            /* Print success message for test framework */
            if (exit_code == 0) {
                klog_info("TEST", "PASSED: syscall");
            } else {
                klog_error("TEST", "FAILED: syscall (exit code=%d)", (int)exit_code);
            }

            system_shutdown();
            /* Should never reach here */
            kernel_halt();
        }

        /* Use process_exit for proper cleanup */
        process_exit((int)exit_code);
        /* Should never reach here */
        kernel_halt();
    } else {
        /* No process - simple ring 3 program, shut down cleanly */
        klog_info("USER", "Process exited with code: %d", (int)exit_code);
        system_shutdown();
    }
}

/**
 * sys_yield - Voluntarily yield the CPU
 *
 * Returns: 0 on success
 */
static int64_t sys_yield(void) {
    klog_debug("SYSCALL", "sys_yield");

    /* Yield to next runnable thread */
    thread_yield();

    return 0;
}

/**
 * sys_getpid - Get the current process ID
 *
 * Returns: Process ID
 */
static int64_t sys_getpid(void) {
    thread_t *t = thread_get_current();
    process_t *p = process_get_current();

    if (t == NULL) {
        klog_warn("SYSCALL", "sys_getpid: no current thread!");
        return -1;
    }

    if (p == NULL) {
        klog_warn("SYSCALL", "sys_getpid: thread=%p has no process (t->process=%p)", t, t->process);
        return -1;
    }

    klog_info("USER", "sys_getpid: returning PID=%d", p->pid);

    return p->pid;
}

/**
 * sys_fork - Create a child process
 *
 * Returns: In parent: child PID > 0
 *          In child: 0
 *          On error: negative error code
 */
static int64_t sys_fork(void) {
    process_t *child;
    thread_t *current = thread_get_current();

    klog_debug("SYSCALL", "sys_fork");

    if (current == NULL || current->process == NULL) {
        klog_error("SYSCALL", "sys_fork: no current process");
        return -1;  /* EPERM */
    }

    /* Fork the process */
    child = process_fork();
    if (child == NULL) {
        klog_error("SYSCALL", "sys_fork: process_fork failed");
        return -12;  /* ENOMEM */
    }

    /* If we're the parent, return child PID */
    if (process_get_current() == current->process) {
        klog_info("SYSCALL", "sys_fork: parent PID=%d returning child PID=%d",
                  current->process->pid, child->pid);
        return child->pid;
    }

    /* If we're the child, return 0 */
    klog_info("SYSCALL", "sys_fork: child PID=%d returning 0",
              process_get_current()->pid);
    return 0;
}

/**
 * sys_wait - Wait for a child process to exit
 * @pid: PID to wait for (-1 = any child)
 * @status: Pointer to store exit status
 *
 * Returns: PID of exited child, or negative error code
 */
static int64_t sys_wait(uint64_t pid, int *status) {
    int ret;

    klog_debug("SYSCALL", "sys_wait: pid=%d, status=%p", (int)pid, status);

    /* Validate status pointer if provided */
    if (status != NULL) {
        if (probe_user_write(status, sizeof(int)) != 0) {
            klog_warn("SYSCALL", "sys_wait: invalid status pointer %p", status);
            return -EFAULT;
        }
    }

    /* Call process_wait */
    ret = process_wait((int)pid, status);

    klog_debug("SYSCALL", "sys_wait: returning %d", ret);

    return ret;
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
        case 3: /* SYS_yield */
            result = sys_yield();
            __asm__ volatile ("mov %0, %%rax" : : "r"(result) : "rax");
            break;
        case 4: /* SYS_getpid */
            result = sys_getpid();
            __asm__ volatile ("mov %0, %%rax" : : "r"(result) : "rax");
            break;
        case 5: /* SYS_fork */
            result = sys_fork();
            __asm__ volatile ("mov %0, %%rax" : : "r"(result) : "rax");
            break;
        case 6: /* SYS_wait */
            result = sys_wait(a1, (int *)a2);
            __asm__ volatile ("mov %0, %%rax" : : "r"(result) : "rax");
            break;
        default:
            klog_warn("SYSCALL", "Unknown syscall: %x", nr);
            __asm__ volatile ("mov $-38, %%rax" ::: "rax");  /* ENOSYS */
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

/* Jump to syscall test program in ring 3 */
void enter_syscall_test_mode(void) {
    klog_info("SYSCALL", "Entering syscall test mode...");

    /* Create test process */
    process_t *test_process = process_create("syscall_test", 0);
    if (test_process == NULL) {
        klog_error("SYSCALL", "Failed to create test process");
        return;
    }
    klog_info("SYSCALL", "Created test process: PID=%d", test_process->pid);

    test_process->state = PROCESS_RUNNING;

    /* CRITICAL FIX: Get the idle thread for this CPU and set it as current
     * This ensures thread_get_current() returns a valid thread pointer during syscalls
     * The idle thread infrastructure is already initialized by scheduler_init() */
    int cpu = smp_get_cpu_index();
    thread_t *idle = scheduler_get_idle_thread(cpu);

    if (idle == NULL) {
        klog_error("SYSCALL", "Failed to get idle thread for CPU %d", cpu);
        return;
    }

    thread_set_current(idle);
    klog_info("SYSCALL", "Set idle thread (tid=%d) as current for CPU %d", idle->tid, cpu);

    /* Associate the idle thread with the test process
     * This allows syscalls to access the process via t->process */
    idle->process = test_process;
    klog_info("SYSCALL", "Associated idle thread with test process (PID=%d)", test_process->pid);

    /* Get address of syscall test program */
    uint64_t user_rip = (uint64_t)syscall_test_start;
    uint64_t user_rsp = (uint64_t)user_stack + user_stack_size;

    klog_info("SYSCALL", "syscall_test_start address: %p", (void*)user_rip);
    klog_info("SYSCALL", "User stack: %p", (void*)user_rsp);

    /* Stack alignment */
    user_rsp &= ~0xF;

    /* RFLAGS: IF=1, bit 1=1 */
    uint64_t user_rflags = 0x202;

    klog_info("SYSCALL", "About to jump to user mode: RIP=%p, RSP=%p", (void*)user_rip, (void*)user_rsp);

    /* Jump to user mode for syscall test */
    jump_to_user_mode(user_rip, user_rsp, user_rflags);

    /* Should never reach here */
    klog_error("SYSCALL", "ERROR: Returned from syscall test!");
}
