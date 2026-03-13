/* Emergence Kernel - Thread Management */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "kernel/klog.h"
#include "kernel/list.h"
#include "kernel/slab.h"
#include "kernel/pmm.h"
#include "kernel/thread.h"
#include "arch/x86_64/smp.h"
#include "include/spinlock.h"
#include "include/string.h"

/* Slab cache for thread structures */
static slab_cache_t *thread_cache = NULL;

/* Thread ID counter */
static int next_tid = 1;

/* List of all threads */
static struct list_head all_threads_list;
static spinlock_t all_threads_lock;

/* Thread statistics */
static uint64_t nr_total_threads;
static uint64_t nr_active_threads;

/* External assembly functions */
extern void context_switch(cpu_context_t *prev, cpu_context_t *next);
extern void thread_entry_wrapper(void);

/**
 * thread_init - Initialize the thread subsystem
 */
void thread_init(void) {
    klog_info("THREAD", "Initializing thread subsystem");

    /* Initialize slab cache for thread_t structures
     * sizeof(thread_t) is 248 bytes, which is not a power of two.
     * Round up to 256 bytes (next power of two) for the slab cache.
     */
    static slab_cache_t thread_cache_data;
    size_t cache_size = 256;  /* Next power of two after 248 */

    if (slab_cache_create(&thread_cache_data, cache_size) < 0) {
        klog_error("THREAD", "Failed to create thread slab cache (size=%zu)", cache_size);
        return;
    }

    thread_cache = &thread_cache_data;
    klog_info("THREAD", "Created thread cache (object_size=%zu)", cache_size);

    /* Initialize all-threads list */
    list_init(&all_threads_list);
    spin_lock_init(&all_threads_lock);
    nr_total_threads = 0;
    nr_active_threads = 0;

    klog_info("THREAD", "Thread subsystem initialized");
}

/**
 * thread_create - Create a new thread
 * @name: Debug name for the thread
 * @func: Entry function
 * @arg: Argument to pass to entry function
 * @stack_size: Stack size in bytes (0 = default 16KB)
 * @flags: Thread creation flags
 *
 * Returns: Pointer to new thread, or NULL on failure
 */
thread_t *thread_create(const char *name,
                        void (*func)(void *), void *arg,
                        size_t stack_size, int flags) {
    thread_t *thread;
    void *stack;
    size_t actual_stack_size;
    int order;

    /* Validate arguments */
    if (name == NULL || func == NULL) {
        return NULL;
    }

    /* Calculate stack size (default 16KB = 4 pages) */
    actual_stack_size = stack_size > 0 ? stack_size : THREAD_DEFAULT_STACK_SIZE;

    /* Round up to page size and calculate order */
    actual_stack_size = (actual_stack_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    order = 0;
    while ((1UL << order) < actual_stack_size / PAGE_SIZE) {
        order++;
    }

    /* Allocate thread structure from slab cache */
    thread = slab_alloc(thread_cache);
    if (thread == NULL) {
        klog_error("THREAD", "Failed to allocate thread structure");
        return NULL;
    }

    /* Zero-initialize thread structure */
    memset(thread, 0, sizeof(thread_t));

    /* Allocate stack using PMM (contiguous pages) */
    stack = pmm_alloc(order);
    if (stack == NULL) {
        klog_error("THREAD", "Failed to allocate stack (order %d)", order);
        slab_free(thread_cache, thread);
        return NULL;
    }

    /* Initialize thread fields */
    thread->tid = next_tid++;

    thread->name = name;
    thread->entry_func = func;
    thread->entry_arg = arg;
    thread->flags = flags;
    thread->state = THREAD_CREATED;
    thread->cpu = -1;
    thread->ticks = 0;

    thread->kernel_stack = stack;
    thread->kernel_stack_size = actual_stack_size;

    /* Initialize list heads */
    list_init(&thread->run_list);
    list_init(&thread->all_list);

    /* Set up initial context for new thread
     * When a new thread is first scheduled, it will start executing at thread_entry_wrapper
     * which will then call the thread's entry function.
     *
     * Stack layout for new thread:
     *   top of stack (initial RSP)
     *   ...
     *   cpu_context_t structure
     *   ...
     *   bottom of stack
     */
    {
        /* Calculate stack top (stack grows downward) */
        uint8_t *stack_top = (uint8_t *)stack + actual_stack_size;

        /* Align stack top to 16 bytes */
        stack_top = (uint8_t *)((uintptr_t)stack_top & ~0xFUL);

        /* Set up context */
        cpu_context_t *ctx = &thread->context;

        /* Set initial register values */
        ctx->r15 = ctx->r14 = ctx->r13 = ctx->r12 = 0;
        ctx->r11 = ctx->r10 = ctx->r9 = ctx->r8 = 0;
        ctx->rbp = (uint64_t)stack_top;  /* Frame pointer = stack top */
        ctx->rdi = 0;  /* First argument (will be set to thread pointer in wrapper) */
        ctx->rsi = ctx->rdx = ctx->rcx = ctx->rbx = 0;
        ctx->rax = 0;

        /* Set instruction pointer to entry wrapper */
        ctx->rip = (uint64_t)thread_entry_wrapper;

        /* Set stack pointer (will be adjusted by context_switch) */
        ctx->rsp = (uint64_t)stack_top;

        /* Enable interrupts for kernel threads */
        ctx->rflags = 0x200;  /* IF bit set (bit 9) */
    }

    /* Add to all-threads list */
    spin_lock(&all_threads_lock);
    list_push_back(&thread->all_list, &all_threads_list);
    nr_total_threads++;
    spin_unlock(&all_threads_lock);

    klog_info("THREAD", "Created thread '%s' (tid=%d, stack=%zu bytes)",
             name, thread->tid, actual_stack_size);

    return thread;
}

/**
 * thread_exit - Terminate the current thread
 *
 * Marks the current thread as terminated and triggers a context switch.
 * This function never returns.
 */
void thread_exit(void) {
    thread_t *current = thread_get_current();

    if (current == NULL) {
        klog_error("THREAD", "thread_exit() called with no current thread");
        __builtin_unreachable();
    }

    klog_debug("THREAD", "Thread '%s' (tid=%d) exiting", current->name, current->tid);

    /* Mark thread as terminated */
    current->state = THREAD_TERMINATED;

    /* Remove from all-threads list */
    spin_lock(&all_threads_lock);
    list_remove(&current->all_list);
    nr_active_threads--;
    spin_unlock(&all_threads_lock);

    /* Trigger context switch - this never returns */
    extern void schedule(void);
    schedule();

    /* Should never reach here */
    klog_error("THREAD", "thread_exit() returned!");
    while (1) {
        __asm__ volatile ("hlt");
    }
}

/**
 * thread_yield - Voluntarily yield the CPU
 *
 * Moves the current thread to the back of the runqueue and triggers
 * a context switch. This allows cooperative multitasking.
 */
void thread_yield(void) {
    thread_t *current = thread_get_current();

    if (current == NULL) {
        return;
    }

    /* Mark as ready and add back to runqueue */
    current->state = THREAD_READY;
    extern void scheduler_add_thread(thread_t *t);
    scheduler_add_thread(current);

    /* Trigger context switch */
    extern void schedule(void);
    schedule();
}

/**
 * thread_get_current - Get the currently running thread
 *
 * Returns: Pointer to current thread, or NULL if none
 */
thread_t *thread_get_current(void) {
    /* Read from per-CPU data at GS offset 48 */
    thread_t *current;
    __asm__ volatile ("mov %%gs:48, %0" : "=r"(current));
    return current;
}

/**
 * thread_set_current - Set the currently running thread
 * @t: Thread to set as current
 */
void thread_set_current(thread_t *t) {
    /* Write to per-CPU data at GS offset 48 */
    __asm__ volatile ("mov %0, %%gs:48" :: "r"(t));
}

/**
 * thread_destroy - Free thread resources
 * @t: Thread to destroy
 *
 * Frees the thread's stack and structure. The thread must already
 * be terminated and removed from all lists.
 */
void thread_destroy(thread_t *t) {
    if (t == NULL) {
        return;
    }

    /* Free stack */
    if (t->kernel_stack != NULL) {
        /* Calculate order from stack size */
        int order = 0;
        size_t pages = t->kernel_stack_size / PAGE_SIZE;
        while ((1UL << order) < pages) {
            order++;
        }
        pmm_free(t->kernel_stack, order);
    }

    /* Free thread structure */
    slab_free(thread_cache, t);
}
