/* Emergence Kernel - x86_64 CPU Context
 *
 * Architecture-specific CPU context definition and operations.
 * This file contains x86_64-specific register layout and low-level
 * context manipulation functions.
 */

#ifndef _ARCH_X86_64_CPU_CONTEXT_H
#define _ARCH_X86_64_CPU_CONTEXT_H

#include <stdint.h>

/* CPU context - saved register state
 * Must match assembly layout in context.S exactly!
 * Total size: 144 bytes (18 * 8)
 *
 * Register classification:
 * - Callee-saved: r15, r14, r13, r12, rbx, rbp (preserved across calls)
 * - Caller-saved: r11, r10, r9, r8, rax, rcx, rdx, rsi, rdi (may be clobbered)
 */
typedef struct cpu_context {
    uint64_t r15;       /* Offset 0: Callee-saved */
    uint64_t r14;       /* Offset 8: Callee-saved */
    uint64_t r13;       /* Offset 16: Callee-saved */
    uint64_t r12;       /* Offset 24: Callee-saved */
    uint64_t r11;       /* Offset 32: Caller-saved */
    uint64_t r10;       /* Offset 40: Caller-saved */
    uint64_t r9;        /* Offset 48: Caller-saved */
    uint64_t r8;        /* Offset 56: Caller-saved */
    uint64_t rbp;       /* Offset 64: Frame pointer */
    uint64_t rdi;       /* Offset 72: First argument */
    uint64_t rsi;       /* Offset 80: Second argument */
    uint64_t rdx;       /* Offset 88: Third argument */
    uint64_t rcx;       /* Offset 96: Fourth argument */
    uint64_t rbx;       /* Offset 104: Callee-saved */
    uint64_t rax;       /* Offset 112: Return value */
    uint64_t rip;       /* Offset 120: Instruction pointer */
    uint64_t rsp;       /* Offset 128: Stack pointer */
    uint64_t rflags;    /* Offset 136: CPU flags */
} __attribute__((packed)) cpu_context_t;

/* RFLAGS bit definitions */
#define RFLAGS_CF   0x0001      /* Carry Flag (bit 0) */
#define RFLAGS_PF   0x0004      /* Parity Flag (bit 2) */
#define RFLAGS_AF   0x0010      /* Auxiliary Carry Flag (bit 4) */
#define RFLAGS_ZF   0x0040      /* Zero Flag (bit 6) */
#define RFLAGS_SF   0x0080      /* Sign Flag (bit 7) */
#define RFLAGS_TF   0x0100      /* Trap Flag (bit 8) */
#define RFLAGS_IF   0x0200      /* Interrupt Enable Flag (bit 9) */
#define RFLAGS_DF   0x0400      /* Direction Flag (bit 10) */
#define RFLAGS_OF   0x0800      /* Overflow Flag (bit 11) */
#define RFLAGS_IOPL 0x3000      /* I/O Privilege Level (bits 12-13) */
#define RFLAGS_NT   0x4000      /* Nested Task (bit 14) */
#define RFLAGS_RF   0x10000     /* Resume Flag (bit 16) */
#define RFLAGS_VM   0x20000     /* Virtual 8086 Mode (bit 17) */
#define RFLAGS_AC   0x40000     /* Alignment Check (bit 18) */
#define RFLAGS_VIF  0x80000     /* Virtual Interrupt Flag (bit 19) */
#define RFLAGS_VIP  0x100000    /* Virtual Interrupt Pending (bit 20) */
#define RFLAGS_ID   0x200000    /* CPUID instruction available (bit 21) */

/* ============================================================================
 * Architecture-specific Thread Context Operations
 * ============================================================================ */

/**
 * arch_context_init - Initialize CPU context for a new thread
 * @ctx: Context to initialize
 * @stack_top: Top of thread stack (stack grows downward)
 * @entry_point: Entry point function (typically thread_entry_wrapper)
 * @rflags: Initial RFLAGS value
 *
 * Initializes all registers to zero except:
 * - rbp: Set to stack_top for frame pointer
 * - rip: Set to entry_point for execution start
 * - rsp: Set to stack_top for initial stack pointer
 * - rflags: Set to enable interrupts for kernel threads
 */
static inline void arch_context_init(cpu_context_t *ctx, void *stack_top,
                                      void *entry_point, uint64_t rflags) {
    /* Zero all callee-saved registers */
    ctx->r15 = 0;
    ctx->r14 = 0;
    ctx->r13 = 0;
    ctx->r12 = 0;

    /* Zero all caller-saved registers */
    ctx->r11 = 0;
    ctx->r10 = 0;
    ctx->r9 = 0;
    ctx->r8 = 0;

    /* Frame pointer = stack top */
    ctx->rbp = (uint64_t)stack_top;

    /* Argument registers (rdi will be set by thread_entry_wrapper) */
    ctx->rdi = 0;
    ctx->rsi = 0;
    ctx->rdx = 0;
    ctx->rcx = 0;
    ctx->rbx = 0;
    ctx->rax = 0;

    /* Execution state */
    ctx->rip = (uint64_t)entry_point;
    ctx->rsp = (uint64_t)stack_top;
    ctx->rflags = rflags;
}

/**
 * arch_get_current_thread - Get current thread from per-CPU data
 *
 * On x86_64, per-CPU data is accessed via the GS segment.
 * The current thread pointer is stored at offset 48 in the per-CPU area.
 *
 * Returns: Pointer to current thread, or NULL if none
 */
static inline void *arch_get_current_thread(void) {
    void *current;
    __asm__ volatile ("mov %%gs:48, %0" : "=r"(current));
    return current;
}

/**
 * arch_set_current_thread - Set current thread in per-CPU data
 * @t: Thread pointer to store
 *
 * Stores the thread pointer at GS offset 48 in the per-CPU area.
 */
static inline void arch_set_current_thread(void *t) {
    __asm__ volatile ("mov %0, %%gs:48" :: "r"(t));
}

/**
 * arch_cpu_halt - Halt CPU until next interrupt
 *
 * Executes HLT instruction which stops instruction execution
 * until an enabled interrupt occurs. Used in idle threads.
 */
static inline void arch_cpu_halt(void) {
    __asm__ volatile ("hlt");
}

/**
 * arch_enable_interrupts - Enable CPU interrupts
 *
 * Executes STI instruction to set the interrupt flag (IF)
 * in RFLAGS, allowing interrupts to be serviced.
 */
static inline void arch_enable_interrupts(void) {
    __asm__ volatile ("sti");
}

/**
 * arch_disable_interrupts - Disable CPU interrupts
 *
 * Executes CLI instruction to clear the interrupt flag (IF)
 * in RFLAGS, preventing interrupt handling.
 *
 * Returns: Previous RFLAGS value (for restore)
 */
static inline uint64_t arch_disable_interrupts(void) {
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags));
    return flags;
}

/**
 * arch_restore_interrupts - Restore interrupt state
 * @flags: RFLAGS value to restore
 *
 * Restores the interrupt flag from a previously saved value.
 */
static inline void arch_restore_interrupts(uint64_t flags) {
    __asm__ volatile ("pushq %0; popfq" :: "r"(flags));
}

#endif /* _ARCH_X86_64_CPU_CONTEXT_H */
