/* Emergence Kernel - Thread Internal Implementation
 *
 * This header contains internal thread implementation details.
 * Public API is in include/kernel/thread.h
 * Architecture-specific context is in arch/x86_64/include/cpu_context.h
 */

#ifndef _KERNEL_THREAD_INTERNAL_H
#define _KERNEL_THREAD_INTERNAL_H

/* Include public API first */
#include "include/kernel/thread.h"

/* Include architecture-specific context */
#include "arch/x86_64/include/cpu_context.h"

/* Internal dependencies */
#include "kernel/list.h"

/* Page size for stack allocation */
#define PAGE_SIZE 4096

/* Thread Control Block - Full internal definition
 *
 * Layout (verified offsets):
 *   0-15:      run_list (16 bytes)
 *   16-31:     all_list (16 bytes)
 *   32-35:     state (4 bytes)
 *   36-39:     tid (4 bytes)
 *   40-43:     flags (4 bytes)
 *   44-47:     padding (4 bytes)
 *   48-55:     name pointer (8 bytes)
 *   56-199:    context (144 bytes)
 *   200-207:   kernel_stack (8 bytes)
 *   208-215:   kernel_stack_size (8 bytes)
 *   216-223:   entry_func (8 bytes)
 *   224-231:   entry_arg (8 bytes)
 *   232-235:   cpu (4 bytes)
 *   236-239:   padding (4 bytes)
 *   240-247:   ticks (8 bytes)
 *   248-255:   process pointer (8 bytes)
 *   256-263:   address_space pointer (8 bytes)
 *   264-271:   user_stack pointer (8 bytes)
 *   272-279:   user_stack_size (8 bytes)
 *   280-287:   user_rsp (8 bytes)
 * Total: 288 bytes (fits in 512B slab cache)
 */
struct thread {
    struct list_head run_list;      /* Runqueue linkage */
    struct list_head all_list;      /* All-threads list linkage */

    thread_state_t state;           /* Current state */
    int tid;                        /* Thread ID (unique) */
    int flags;                      /* Thread flags */
    const char *name;               /* Debug name */

    cpu_context_t context;          /* Saved register state (arch-specific) */

    void *kernel_stack;             /* Stack base (from PMM) */
    size_t kernel_stack_size;       /* Stack size in bytes */

    void (*entry_func)(void *);     /* Entry function */
    void *entry_arg;                /* Entry argument */

    int cpu;                        /* Last CPU that ran this thread (-1 = any) */
    uint64_t ticks;                 /* CPU time consumed (timer ticks) */

    /* Process and address space management */
    process_t *process;             /* Owning process (NULL for kernel threads) */
    address_space_t *as;            /* Address space (NULL for kernel threads) */

    /* User mode execution state */
    void *user_stack;               /* User stack base (for user threads) */
    size_t user_stack_size;         /* User stack size in bytes */
    uint64_t user_rsp;              /* Saved user RSP during syscall */
};

/* Assembly context switch function - implemented in context.S */
void context_switch(cpu_context_t *prev, cpu_context_t *next);

/* Assembly entry wrapper - implemented in context.S */
void thread_entry_wrapper(void);

#endif /* _KERNEL_THREAD_INTERNAL_H */
