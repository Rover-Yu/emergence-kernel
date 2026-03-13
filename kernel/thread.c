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
#include "arch/x86_64/include/cpu_context.h"
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
     * sizeof(thread_t) is 288 bytes, which is not a power of two.
     * Round up to 512 bytes (next power of two) for the slab cache.
     */
    static slab_cache_t thread_cache_data;
    size_t cache_size = 512;  /* Next power of two after 288 */

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

    /* Set up initial context for new thread using architecture abstraction
     * When a new thread is first scheduled, it will start executing at
     * thread_entry_wrapper which will then call the thread's entry function.
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

        /* Initialize context using architecture abstraction */
        arch_context_init(&thread->context, stack_top,
                         thread_entry_wrapper, RFLAGS_IF);
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
        arch_cpu_halt();
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
    return (thread_t *)arch_get_current_thread();
}

/**
 * thread_set_current - Set the currently running thread
 * @t: Thread to set as current
 */
void thread_set_current(thread_t *t) {
    arch_set_current_thread(t);
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

/**
 * thread_get_tid - Get thread ID
 * @t: Thread to query
 *
 * Returns: Thread ID, or -1 if t is NULL
 */
int thread_get_tid(thread_t *t) {
    return t ? t->tid : -1;
}

/**
 * thread_get_state - Get thread state
 * @t: Thread to query
 *
 * Returns: Thread state, or THREAD_TERMINATED if t is NULL
 */
thread_state_t thread_get_state(thread_t *t) {
    return t ? t->state : THREAD_TERMINATED;
}

/**
 * thread_get_name - Get thread name
 * @t: Thread to query
 *
 * Returns: Thread name string, or NULL if t is NULL
 */
const char *thread_get_name(thread_t *t) {
    return t ? t->name : NULL;
}
