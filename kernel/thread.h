/* Emergence Kernel - Thread Management */

#ifndef _KERNEL_THREAD_H
#define _KERNEL_THREAD_H

#include <stdint.h>
#include <stddef.h>
#include "kernel/list.h"

/* Thread states */
typedef enum {
    THREAD_CREATED,     /* Thread created but not yet runnable */
    THREAD_READY,       /* Thread is ready to run */
    THREAD_RUNNING,     /* Thread is currently running */
    THREAD_BLOCKED,     /* Thread is blocked (waiting for I/O, etc.) */
    THREAD_TERMINATED   /* Thread has exited */
} thread_state_t;

/* CPU context - saved register state
 * Must match assembly layout in context.S exactly!
 * Total size: 144 bytes (18 * 8) */
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

/* Thread flags */
#define THREAD_FLAG_KERNEL  0x0001  /* Kernel thread (vs user thread) */

/* Default stack size (16KB = 4 pages) */
#define THREAD_DEFAULT_STACK_SIZE 16384

/* Thread Control Block
 *
 * Layout (verified offsets):
 *   0-15:   run_list (16 bytes)
 *   16-31:  all_list (16 bytes)
 *   32-35:  state (4 bytes)
 *   36-39:  tid (4 bytes)
 *   40-43:  flags (4 bytes)
 *   44-47:  padding (4 bytes)
 *   48-55:  name pointer (8 bytes)
 *   56-199: context (144 bytes)
 *   200-207: kernel_stack (8 bytes)
 *   208-215: kernel_stack_size (8 bytes)
 *   216-223: entry_func (8 bytes)
 *   224-231: entry_arg (8 bytes)
 *   232-235: cpu (4 bytes)
 *   236-239: padding (4 bytes)
 *   240-247: ticks (8 bytes)
 * Total: 248 bytes (fits in 256B slab cache)
 */
struct thread {
    struct list_head run_list;      /* Runqueue linkage */
    struct list_head all_list;      /* All-threads list linkage */

    thread_state_t state;           /* Current state */
    int tid;                        /* Thread ID (unique) */
    int flags;                      /* Thread flags */
    const char *name;               /* Debug name */

    cpu_context_t context;          /* Saved register state */

    void *kernel_stack;             /* Stack base (from PMM) */
    size_t kernel_stack_size;       /* Stack size in bytes */

    void (*entry_func)(void *);     /* Entry function */
    void *entry_arg;                /* Entry argument */

    int cpu;                        /* Last CPU that ran this thread (-1 = any) */
    uint64_t ticks;                 /* CPU time consumed (timer ticks) */
};

typedef struct thread thread_t;

/* Thread API */

/**
 * thread_init - Initialize the thread subsystem
 *
 * Creates slab cache for thread_t structures.
 * Must be called after slab_init().
 */
void thread_init(void);

/**
 * thread_create - Create a new thread
 * @name: Debug name for the thread
 * @func: Entry function
 * @arg: Argument to pass to entry function
 * @stack_size: Stack size in bytes (0 = default 16KB)
 * @flags: Thread creation flags (THREAD_FLAG_KERNEL, etc.)
 *
 * Returns: Pointer to new thread, or NULL on failure
 */
thread_t *thread_create(const char *name,
                        void (*func)(void *), void *arg,
                        size_t stack_size, int flags);

/**
 * thread_exit - Terminate the current thread
 *
 * Marks the current thread as terminated and triggers a context switch.
 * This function does not return.
 */
void thread_exit(void) __attribute__((noreturn));

/**
 * thread_yield - Voluntarily yield the CPU
 *
 * Moves the current thread to the back of the runqueue
 * and triggers a context switch to the next ready thread.
 */
void thread_yield(void);

/**
 * thread_get_current - Get the currently running thread
 *
 * Returns: Pointer to current thread, or NULL if no thread is running
 */
thread_t *thread_get_current(void);

/**
 * thread_set_current - Set the current thread for this CPU
 * @t: Thread to set as current
 */
void thread_set_current(thread_t *t);

/**
 * thread_destroy - Free thread resources
 * @t: Thread to destroy
 *
 * Frees the thread's stack and returns the thread_t to the slab cache.
 * The thread must be in TERMINATED state.
 */
void thread_destroy(thread_t *t);

/* Assembly context switch function - implemented in context.S */
void context_switch(cpu_context_t *prev, cpu_context_t *next);

/* Assembly entry wrapper - implemented in context.S */
void thread_entry_wrapper(void);

#endif /* _KERNEL_THREAD_H */
