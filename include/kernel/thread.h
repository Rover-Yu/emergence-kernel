/* Emergence Kernel - Thread Public API
 *
 * This header provides the public interface for the thread subsystem.
 * Architecture-specific details are hidden behind opaque types.
 */

#ifndef _KERNEL_THREAD_H
#define _KERNEL_THREAD_H

#include <stdint.h>
#include <stddef.h>

/* Forward declarations for opaque types */
typedef struct cpu_context cpu_context_t;
typedef struct address_space address_space_t;
typedef struct process process_t;

/* Thread states */
typedef enum {
    THREAD_CREATED,     /* Thread created but not yet runnable */
    THREAD_READY,       /* Thread is ready to run */
    THREAD_RUNNING,     /* Thread is currently running */
    THREAD_BLOCKED,     /* Thread is blocked (waiting for I/O, etc.) */
    THREAD_TERMINATED   /* Thread has exited */
} thread_state_t;

/* Thread flags */
#define THREAD_FLAG_KERNEL  0x0001  /* Kernel thread (vs user thread) */
#define THREAD_FLAG_USER    0x0002  /* User thread (ring 3) */

/* Default stack size (16KB = 4 pages) */
#define THREAD_DEFAULT_STACK_SIZE 16384

/* Thread Control Block (opaque forward declaration) */
struct thread;
typedef struct thread thread_t;

/* ============================================================================
 * Thread Lifecycle API
 * ============================================================================ */

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
 * thread_destroy - Free thread resources
 * @t: Thread to destroy
 *
 * Frees the thread's stack and returns the thread_t to the slab cache.
 * The thread must be in TERMINATED state.
 */
void thread_destroy(thread_t *t);

/* ============================================================================
 * Thread State Query API
 * ============================================================================ */

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

/* ============================================================================
 * Thread Property Accessors
 * ============================================================================ */

/**
 * thread_get_tid - Get thread ID
 * @t: Thread to query
 *
 * Returns: Thread ID, or -1 if t is NULL
 */
int thread_get_tid(thread_t *t);

/**
 * thread_get_state - Get thread state
 * @t: Thread to query
 *
 * Returns: Thread state, or THREAD_TERMINATED if t is NULL
 */
thread_state_t thread_get_state(thread_t *t);

/**
 * thread_get_name - Get thread name
 * @t: Thread to query
 *
 * Returns: Thread name string, or NULL if t is NULL
 */
const char *thread_get_name(thread_t *t);

#endif /* _KERNEL_THREAD_H */
