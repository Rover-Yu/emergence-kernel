/* Emergence Kernel - FIFO Scheduler Internal Implementation
 *
 * This header contains internal scheduler implementation details.
 * Public API is in include/kernel/scheduler.h
 * Architecture-specific operations are in arch/x86_64/include/cpu_context.h
 */

#ifndef _KERNEL_SCHEDULER_INTERNAL_H
#define _KERNEL_SCHEDULER_INTERNAL_H

/* Include public API first */
#include "include/kernel/scheduler.h"

/* Include architecture-specific context */
#include "arch/x86_64/include/cpu_context.h"

/* Internal dependencies */
#include "kernel/thread.h"
#include "kernel/list.h"
#include "include/spinlock.h"
#include "arch/x86_64/smp.h"  /* For SMP_MAX_CPUS */

/* Scheduler tick interval - trigger context switch every N timer ticks */
#define SCHEDULER_TICK_INTERVAL 5

/* Global runqueue structure */
struct runqueue {
    spinlock_t lock;                            /* Protects runqueue operations */
    struct list_head thread_list;               /* FIFO queue of runnable threads */
    int nr_running;                             /* Number of runnable threads */
    thread_t *idle_threads[SMP_MAX_CPUS];      /* Per-CPU idle threads */
};

typedef struct runqueue runqueue_t;

/* Idle thread function - forward declaration */
void idle_thread_func(void *arg);

/* Internal accessor for tests - forward declaration */
runqueue_t *scheduler_get_runqueue(void);

#endif /* _KERNEL_SCHEDULER_INTERNAL_H */
