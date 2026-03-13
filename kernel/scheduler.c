/* Emergence Kernel - FIFO Scheduler */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "kernel/klog.h"
#include "kernel/thread.h"
#include "kernel/scheduler.h"
#include "kernel/pmm.h"
#include "kernel/list.h"
#include "include/spinlock.h"
#include "include/string.h"
#include "arch/x86_64/smp.h"

/* Global runqueue */
static runqueue_t runqueue;

/* Scheduler tick counter for preemptive scheduling */
static uint64_t scheduler_tick_counter;

/* Forward declarations for external functions */
extern void context_switch(cpu_context_t *prev, cpu_context_t *next);

/* Forward declaration for internal helper */
static thread_t *pick_next_thread(void);

/**
 * scheduler_init - Initialize the scheduler
 *
 * Creates the global runqueue and per-CPU idle threads.
 * Must be called after thread_init() and smp_init().
 */
void scheduler_init(void) {
    int i;

    klog_info("SCHED", "Initializing scheduler");

    /* Initialize runqueue */
    spin_lock_init(&runqueue.lock);
    list_init(&runqueue.thread_list);
    runqueue.nr_running = 0;

    /* Clear idle thread pointers */
    for (i = 0; i < SMP_MAX_CPUS; i++) {
        runqueue.idle_threads[i] = NULL;
    }

    /* Create idle threads for each CPU */
    for (i = 0; i < SMP_MAX_CPUS; i++) {
        char name[16];
        thread_t *idle;

        snprintf(name, sizeof(name), "idle_cpu%d", i);

        idle = thread_create(name, idle_thread_func, (void *)(uintptr_t)i,
                        THREAD_DEFAULT_STACK_SIZE, THREAD_FLAG_KERNEL);
        if (idle == NULL) {
            klog_error("SCHED", "Failed to create idle thread for CPU %d", i);
            continue;
        }

        runqueue.idle_threads[i] = idle;
        klog_info("SCHED", "Created idle thread for CPU %d (tid=%d)", i, idle->tid);
    }

    klog_info("SCHED", "Scheduler initialized");
}

/**
 * idle_thread_func - Idle thread entry point
 * @arg: CPU index (cast to void*)
 *
 * Loops forever with HLT instruction. Each CPU has its own idle thread.
 */
void idle_thread_func(void *arg) {
    int cpu = (int)(uintptr_t)arg;

    klog_debug("SCHED", "Idle thread started on CPU %d", cpu);

    /* Idle loop - HLT halts until next interrupt */
    while (1) {
        __asm__ volatile ("hlt");
    }
}

/**
 * scheduler_start - Start scheduling on the current CPU
 *
 * This function never returns. It switches to the idle thread
 * and enters the scheduling loop.
 */
void scheduler_start(void) {
    thread_t *current = thread_get_current();
    thread_t *idle;

    /* If no current thread, switch to idle */
    if (current == NULL) {
        int cpu = smp_get_cpu_index();
        idle = runqueue.idle_threads[cpu];
        if (idle != NULL) {
            thread_set_current(idle);
            idle->state = THREAD_RUNNING;
            /* Jump to idle thread context */
            context_switch(NULL, &idle->context);
        }
        /* Should never reach here */
    }

    /* Enable interrupts and enter scheduling loop */
    __asm__ volatile ("sti");

    /* This point should never be reached */
    while (1) {
        __asm__ volatile ("hlt");
    }
}

/**
 * schedule - Perform a context switch
 *
 * Picks the next thread from the runqueue and switches to it.
 * If no threads are available, runs the idle thread.
 * Must be called with interrupts disabled.
 */
void schedule(void) {
    thread_t *prev, *next;

    /* Get current thread */
    prev = thread_get_current();

    /* Pick next thread from runqueue */
    next = pick_next_thread();

    /* If no thread available, use idle thread */
    if (next == NULL) {
        int cpu = smp_get_cpu_index();
        next = runqueue.idle_threads[cpu];
    }

    /* No thread to run - should never happen */
    if (next == NULL) {
        klog_error("SCHED", "No thread to run!");
        return;
    }

    /* Same thread - no switch needed */
    if (prev == next) {
        return;
    }

    /* Update thread states */
    if (prev != NULL) {
        /* Put previous thread back on runqueue if it's still runnable */
        if (prev->state != THREAD_TERMINATED && prev->state != THREAD_BLOCKED) {
            prev->state = THREAD_READY;
            scheduler_add_thread(prev);
        }
    }

    next->state = THREAD_RUNNING;
    next->cpu = smp_get_cpu_index();

    /* Set current thread */
    thread_set_current(next);

    /* Perform context switch */
    context_switch(&prev->context, &next->context);
}

/**
 * scheduler_tick - Called from timer interrupt
 *
 * Called every timer tick to check if a context switch is needed.
 * Triggers schedule() every SCHEDULER_TICK_INTERVAL ticks.
 */
void scheduler_tick(void) {
    /* Increment tick counter */
    scheduler_tick_counter++;

    /* Check if it's time for a context switch */
    if ((scheduler_tick_counter % SCHEDULER_TICK_INTERVAL) == 0) {
        /* Time to schedule */
        schedule();
    }
}

/**
 * scheduler_add_thread - Add a thread to the runqueue
 * @t: Thread to add (must be in READY state)
 */
void scheduler_add_thread(thread_t *t) {
    irq_flags_t flags;

    if (t == NULL) {
        return;
    }

    /* Ensure thread is in READY state */
    if (t->state != THREAD_READY) {
        t->state = THREAD_READY;
    }

    /* Acquire runqueue lock with interrupts disabled */
    flags = spin_lock_irqsave(&runqueue.lock);

    /* Add to back of FIFO queue */
    list_push_back(&t->run_list, &runqueue.thread_list);
    runqueue.nr_running++;

    spin_unlock_irqrestore(&runqueue.lock, flags);
}

 /**
  * scheduler_remove_thread - Remove a thread from the runqueue
  * @t: Thread to remove
  */
void scheduler_remove_thread(thread_t *t) {
    irq_flags_t flags;

    if (t == NULL) {
        return;
    }

    /* Acquire runqueue lock */
    flags = spin_lock_irqsave(&runqueue.lock);

    /* Remove from runqueue */
    list_remove(&t->run_list);
    runqueue.nr_running--;

    spin_unlock_irqrestore(&runqueue.lock, flags);
}

/**
 * scheduler_get_nr_running - Get number of runnable threads
 *
 * Returns: Number of threads in the runqueue
 */
int scheduler_get_nr_running(void) {
    return runqueue.nr_running;
}

/**
 * scheduler_get_runqueue - Get the global runqueue
 *
 * Returns: Pointer to the global runqueue
 */
runqueue_t *scheduler_get_runqueue(void) {
    return &runqueue;
}

/**
 * scheduler_get_idle_thread - Get the idle thread for a CPU
 * @cpu: CPU index
 *
 * Returns: Pointer to the idle thread for the specified CPU, or NULL
 */
thread_t *scheduler_get_idle_thread(int cpu) {
    if (cpu < 0 || cpu >= SMP_MAX_CPUS) {
        return NULL;
    }
    return runqueue.idle_threads[cpu];
}

/**
 * pick_next_thread - Pick next thread from runqueue (FIFO order)
 *
 * Returns: Next thread to run, or NULL if runqueue is empty
 */
static thread_t *pick_next_thread(void) {
    thread_t *next = NULL;
    irq_flags_t flags;

    /* Acquire runqueue lock */
    flags = spin_lock_irqsave(&runqueue.lock);

    /* Check if runqueue is empty */
    if (list_empty(&runqueue.thread_list)) {
        spin_unlock_irqrestore(&runqueue.lock, flags);
        return NULL;
    }

    /* Get next thread from front of queue (FIFO) */
        struct list_head *node = list_pop_front(&runqueue.thread_list);
        next = list_entry(node, thread_t, run_list);
        runqueue.nr_running--;

    spin_unlock_irqrestore(&runqueue.lock, flags);

    return next;
}







