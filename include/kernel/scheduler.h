/* Emergence Kernel - Scheduler Public API
 *
 * This header provides the public interface for the scheduler subsystem.
 * The scheduler manages thread execution and CPU allocation.
 */

#ifndef _KERNEL_SCHEDULER_H
#define _KERNEL_SCHEDULER_H

#include "include/kernel/thread.h"

/* ============================================================================
 * Scheduler Lifecycle API
 * ============================================================================ */

/**
 * scheduler_init - Initialize the scheduler
 *
 * Creates the global runqueue and per-CPU idle threads.
 * Must be called after thread_init() and smp_init().
 */
void scheduler_init(void);

/**
 * scheduler_start - Start scheduling on the current CPU
 *
 * Enables scheduling and enters the scheduler loop.
 * Should be called by each CPU after initialization.
 * This function never returns.
 */
void scheduler_start(void) __attribute__((noreturn));

/* ============================================================================
 * Thread Scheduling API
 * ============================================================================ */

/**
 * schedule - Perform a context switch
 *
 * Picks the next thread from the runqueue and switches to it.
 * If no threads are available, runs the idle thread.
 * Must be called with interrupts disabled.
 */
void schedule(void);

/**
 * scheduler_add_thread - Add a thread to the runqueue
 * @t: Thread to add (will be set to READY state)
 *
 * Adds the thread to the back of the FIFO queue.
 * Safe to call from interrupt context.
 */
void scheduler_add_thread(thread_t *t);

/**
 * scheduler_remove_thread - Remove a thread from the runqueue
 * @t: Thread to remove
 *
 * Removes the thread from the runqueue.
 * Safe to call from interrupt context.
 */
void scheduler_remove_thread(thread_t *t);

/* ============================================================================
 * Scheduler Tick API
 * ============================================================================ */

/**
 * scheduler_tick - Called from timer interrupt
 *
 * Called every timer tick to check if a context switch is needed.
 * Triggers schedule() at configured intervals.
 */
void scheduler_tick(void);

/* ============================================================================
 * Scheduler Query API
 * ============================================================================ */

/**
 * scheduler_get_nr_running - Get number of runnable threads
 *
 * Returns: Number of threads in the runqueue
 */
int scheduler_get_nr_running(void);

/**
 * scheduler_get_idle_thread - Get the idle thread for a CPU
 * @cpu: CPU index
 *
 * Returns: Pointer to the idle thread for the specified CPU, or NULL
 */
thread_t *scheduler_get_idle_thread(int cpu);

#endif /* _KERNEL_SCHEDULER_H */
