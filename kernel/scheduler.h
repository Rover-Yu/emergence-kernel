/* Emergence Kernel - FIFO Scheduler */

#ifndef _KERNEL_SCHEDULER_H
#define _KERNEL_SCHEDULER_H

#include <stdint.h>
#include "kernel/thread.h"
#include "kernel/list.h"
#include "include/spinlock.h"
#include "arch/x86_64/smp.h"  /* For SMP_MAX_CPUS */

/* Scheduler tick interval - trigger context switch every N timer ticks */
#define SCHEDULER_TICK_INTERVAL 5

/* Default thread stack size (16 KB) */
#define THREAD_DEFAULT_STACK_SIZE 16384

/* Global runqueue structure */
struct runqueue {
    spinlock_t lock;                            /* Protects runqueue operations */
    struct list_head thread_list;               /* FIFO queue of runnable threads */
    int nr_running;                             /* Number of runnable threads */
    thread_t *idle_threads[SMP_MAX_CPUS];      /* Per-CPU idle threads */
};

typedef struct runqueue runqueue_t;

/* Scheduler API */

/**
 * scheduler_init - Initialize the scheduler
 *
 * Creates the global runqueue and per-CPU idle threads.
 * Must be called after thread_init() and smp_init().
 */
void scheduler_init(void);

/**
 * scheduler_start - Start scheduling (does not return)
 *
 * Enables scheduling and enters the scheduler loop.
 * Should be called by each CPU after initialization.
 * This function never returns.
 */
void scheduler_start(void) __attribute__((noreturn));

/**
 * schedule - Perform a context switch
 *
 * Picks the next thread from the runqueue and switches to it.
 * If no threads are available, runs the idle thread.
 * Must be called with interrupts disabled.
 */
void schedule(void);

/**
 * scheduler_tick - Called from timer interrupt
 *
 * Called every timer tick to check if a context switch is needed.
 * Triggers schedule() every SCHEDULER_TICK_INTERVAL ticks.
 */
void scheduler_tick(void);

/**
 * scheduler_add_thread - Add a thread to the runqueue
 * @t: Thread to add (must be in READY state)
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

/**
 * scheduler_get_nr_running - Get number of runnable threads
 *
 * Returns: Number of threads in the runqueue
 */
int scheduler_get_nr_running(void);

/**
 * scheduler_get_runqueue - Get the global runqueue
 *
 * Returns: Pointer to the global runqueue
 */
runqueue_t *scheduler_get_runqueue(void);

/**
 * scheduler_get_idle_thread - Get the idle thread for a CPU
 * @cpu: CPU index
 *
 * Returns: Pointer to the idle thread for the specified CPU, or NULL
 */
thread_t *scheduler_get_idle_thread(int cpu);

/**
 * idle_thread_func - Idle thread entry point
 * @arg: CPU index (cast to void*)
 *
 * Loops forever with HLT instruction. Each CPU has its own idle thread.
 */
void idle_thread_func(void *arg);

#endif /* _KERNEL_SCHEDULER_H */
