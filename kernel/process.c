/* Emergence Kernel - Process Management Implementation
 *
 * Provides process control and lifecycle management for user processes.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "kernel/process.h"
#include "kernel/thread.h"
#include "kernel/vm.h"
#include "kernel/scheduler.h"
#include "kernel/slab.h"
#include "kernel/pmm.h"
#include "kernel/klog.h"
#include "include/string.h"

/* External: kernel_halt from main.c */
extern void kernel_halt(void);

/* Slab cache for process_t structures */
static slab_cache_t process_cache_struct;
static slab_cache_t *process_cache = &process_cache_struct;

/* Global process list */
static struct list_head process_list;
static uint64_t process_list_lock;

/* PID allocator */
static int next_pid = 1;
static uint64_t pid_lock;

/* Idle "init" process (PID 1) */
static process_t *init_process = NULL;

/**
 * process_init - Initialize the process subsystem
 */
void process_init(void)
{
    int ret;

    klog_info("PROC", "Initializing process subsystem");

    /* Initialize global process list */
    list_init(&process_list);
    process_list_lock = 0;
    pid_lock = 0;

    /* Create slab cache for process_t structures */
    /* Note: slab_cache_create requires power-of-two size, so use 256 bytes */
    size_t process_size = sizeof(process_t);
    size_t cache_size = 256;  /* Next power of two that fits process_t */
    if (process_size > cache_size) {
        cache_size = 512;  /* Use larger size if needed */
    }
    ret = slab_cache_create(process_cache, cache_size);
    if (ret != 0) {
        klog_error("PROC", "Failed to create process slab cache");
        return;
    }

    /* Create init process (PID 1) */
    init_process = process_create("init", 0);
    if (init_process == NULL) {
        klog_error("PROC", "Failed to create init process");
        return;
    }
    init_process->pid = 1;
    next_pid = 2;  /* Next PID will be 2 */

    klog_info("PROC", "Process subsystem initialized (init PID=%d)", init_process->pid);
}

/**
 * process_alloc_pid - Allocate a new PID
 *
 * Returns: New PID, or negative error code on failure
 */
int process_alloc_pid(void)
{
    int pid;

    /* Simple linear allocator with wrap-around */
    /* In production, this should track free PIDs for reuse */

    /* TODO: Use proper atomic/spinlock for pid_lock */
    pid = next_pid++;

    if (pid >= PID_MAX) {
        pid = -1;  /* Out of PIDs */
    }

    return pid;
}

/**
 * process_free_pid - Free a PID
 * @pid: PID to free
 */
void process_free_pid(int pid)
{
    /* TODO: Add PID to free list for reuse */
    (void)pid;
}

/**
 * process_create - Create a new process
 * @name: Process name
 * @flags: Process creation flags
 *
 * Returns: Pointer to new process, or NULL on failure
 */
process_t *process_create(const char *name, int flags)
{
    process_t *p;

    (void)flags;  /* Unused for now */

    klog_debug("PROC", "Creating process: %s", name);

    /* Allocate process structure */
    p = slab_alloc(process_cache);
    if (p == NULL) {
        klog_error("PROC", "Failed to allocate process structure");
        return NULL;
    }

    /* Clear structure */
    memset(p, 0, sizeof(process_t));

    /* Allocate PID */
    p->pid = process_alloc_pid();
    if (p->pid < 0) {
        klog_error("PROC", "Failed to allocate PID");
        slab_free(process_cache, p);
        return NULL;
    }

    /* Set name */
    if (name != NULL) {
        strncpy(p->name, name, PROCESS_NAME_MAX - 1);
        p->name[PROCESS_NAME_MAX - 1] = '\0';
    } else {
        strcpy(p->name, "unknown");
    }

    /* Initialize fields */
    p->ppid = 0;
    p->state = PROCESS_CREATED;
    p->exit_status = 0;
    p->flags = 0;
    p->parent = NULL;
    p->waiter = NULL;
    p->exit_lock = 0;
    p->main_thread = NULL;
    p->vm = NULL;
    p->fd_count = 0;
    p->thread_count = 0;

    /* Initialize lists */
    list_init(&p->children);
    list_init(&p->siblings);
    list_init(&p->threads);

    /* Add to global process list */
    list_push_back(&process_list, &p->all_list);

    klog_info("PROC", "Created process '%s' (PID=%d)", p->name, p->pid);

    return p;
}

/**
 * process_get_current - Get the current process
 *
 * Returns: Pointer to current process, or NULL if no process running
 */
process_t *process_get_current(void)
{
    thread_t *t = thread_get_current();

    if (t == NULL) {
        return NULL;
    }

    return t->process;
}

/**
 * process_get_by_pid - Find a process by PID
 * @pid: Process ID to find
 *
 * Returns: Pointer to process, or NULL if not found
 */
process_t *process_get_by_pid(int pid)
{
    struct list_head *pos;

    list_for_each(pos, &process_list) {
        process_t *p = list_entry(pos, process_t, all_list);
        if (p->pid == pid) {
            return p;
        }
    }

    return NULL;
}

/**
 * process_add_thread - Add a thread to a process
 * @p: Process
 * @t: Thread to add
 */
void process_add_thread(process_t *p, thread_t *t)
{
    if (p == NULL || t == NULL) {
        return;
    }

    /* Link thread to process */
    t->process = p;
    t->as = p->vm;

    /* Add to process's thread list */
    list_push_back(&p->threads, &t->run_list);  /* Use run_list for linkage */
    p->thread_count++;

    /* If first thread, set as main_thread */
    if (p->main_thread == NULL) {
        p->main_thread = t;
    }

    klog_debug("PROC", "Added thread TID=%d to process PID=%d (count=%d)",
               t->tid, p->pid, p->thread_count);
}

/**
 * process_remove_thread - Remove a thread from a process
 * @p: Process
 * @t: Thread to remove
 */
void process_remove_thread(process_t *p, thread_t *t)
{
    if (p == NULL || t == NULL) {
        return;
    }

    /* Remove from process's thread list */
    list_remove(&t->run_list);
    p->thread_count--;

    /* Clear cross-references */
    t->process = NULL;

    /* If this was the main thread, clear reference */
    if (p->main_thread == t) {
        p->main_thread = NULL;
    }

    klog_debug("PROC", "Removed thread TID=%d from process PID=%d (count=%d)",
               t->tid, p->pid, p->thread_count);

    /* If no threads left, process becomes zombie */
    if (p->thread_count == 0 && p->state != PROCESS_DEAD) {
        p->state = PROCESS_ZOMBIE;
        klog_info("PROC", "Process PID=%d is now zombie (exit_status=%d)",
                  p->pid, p->exit_status);

        /* Wake up parent if waiting */
        if (p->parent != NULL && p->parent->waiter != NULL) {
            thread_t *waiter = p->parent->waiter;
            p->parent->waiter = NULL;
            waiter->state = THREAD_READY;
            scheduler_add_thread(waiter);
            klog_debug("PROC", "Woke up parent PID=%d waiting for child", p->parent->pid);
        }
    }
}

/**
 * process_fork - Fork the current process
 *
 * Returns: Pointer to child process (in parent), or NULL on failure
 */
process_t *process_fork(void)
{
    process_t *parent, *child;
    thread_t *parent_thread, *child_thread;
    address_space_t *child_vm;

    parent = process_get_current();
    if (parent == NULL) {
        klog_error("PROC", "fork: no current process");
        return NULL;
    }

    parent_thread = thread_get_current();
    if (parent_thread == NULL) {
        klog_error("PROC", "fork: no current thread");
        return NULL;
    }

    klog_info("PROC", "Forking process PID=%d", parent->pid);

    /* Clone address space */
    child_vm = vm_clone_address_space(parent->vm);
    if (child_vm == NULL) {
        klog_error("PROC", "fork: failed to clone address space");
        return NULL;
    }

    /* Create child process */
    child = process_create(parent->name, 0);
    if (child == NULL) {
        klog_error("PROC", "fork: failed to create child process");
        vm_destroy_address_space(child_vm);
        return NULL;
    }

    /* Set up parent/child relationship */
    child->parent = parent;
    child->ppid = parent->pid;
    child->vm = child_vm;
    child->state = PROCESS_CREATED;

    /* Add to parent's children list */
    list_push_back(&parent->children, &child->siblings);

    /* Create child thread */
    child_thread = thread_create(child->name,
                                  parent_thread->entry_func,
                                  parent_thread->entry_arg,
                                  parent_thread->kernel_stack_size,
                                  THREAD_FLAG_USER);
    if (child_thread == NULL) {
        klog_error("PROC", "fork: failed to create child thread");
        vm_destroy_address_space(child_vm);
        slab_free(process_cache, child);
        return NULL;
    }

    /* Set up child thread state */
    child_thread->process = child;
    child_thread->as = child_vm;
    child_thread->flags = THREAD_FLAG_USER;
    /* TODO: Copy register state from parent */

    /* Add thread to process */
    process_add_thread(child, child_thread);

    /* Add child to scheduler */
    child->state = PROCESS_RUNNING;
    child_thread->state = THREAD_READY;
    scheduler_add_thread(child_thread);

    klog_info("PROC", "Fork successful: parent PID=%d -> child PID=%d",
              parent->pid, child->pid);

    return child;
}

/**
 * process_exit - Exit the current process
 * @exit_code: Exit status code
 */
void process_exit(int exit_code)
{
    process_t *p;

    p = process_get_current();
    if (p == NULL) {
        klog_error("PROC", "exit: no current process");
        kernel_halt();
    }

    klog_info("PROC", "Process PID=%d exiting with code %d", p->pid, exit_code);

    /* Set exit status and state */
    p->exit_status = exit_code;
    p->state = PROCESS_ZOMBIE;

    /* TODO: Close all file descriptors */
    /* TODO: Notify parent */
    /* TODO: Reap child processes (give to init) */

    /* Exit current thread - this will trigger process cleanup */
    thread_exit();
}

/**
 * process_wait - Wait for a child process to exit
 * @pid: PID to wait for (-1 = any child)
 * @status: Pointer to store exit status
 *
 * Returns: PID of exited child, or negative error code on failure
 */
int process_wait(int pid, int *status)
{
    process_t *current;
    struct list_head *pos;

    current = process_get_current();
    if (current == NULL) {
        return -1;  /* ESRCH */
    }

    klog_debug("PROC", "Process PID=%d waiting for child PID=%d", current->pid, pid);

    /* Find exited child */
    list_for_each(pos, &current->children) {
        process_t *child = list_entry(pos, process_t, siblings);

        if (pid != -1 && child->pid != pid) {
            continue;
        }

        if (child->state == PROCESS_ZOMBIE) {
            /* Child has exited - reap it */
            if (status != NULL) {
                *status = child->exit_status;
            }

            int child_pid = child->pid;

            /* Remove from children list */
            list_remove(&child->siblings);

            /* Reap child process */
            process_reap(child);

            return child_pid;
        }
    }

    /* No exited child found - block current thread */
    /* TODO: Implement proper blocking wait */
    klog_debug("PROC", "No exited child found, would block");

    return -1;  /* ECHILD */
}

/**
 * process_reap - Reap a zombie process
 * @p: Zombie process to reap
 */
void process_reap(process_t *p)
{
    if (p == NULL) {
        return;
    }

    klog_debug("PROC", "Reaping zombie process PID=%d", p->pid);

    /* Destroy address space */
    if (p->vm != NULL) {
        vm_destroy_address_space(p->vm);
        p->vm = NULL;
    }

    /* Remove from global process list */
    list_remove(&p->all_list);

    /* Free PID */
    process_free_pid(p->pid);

    /* Free process structure */
    slab_free(process_cache, p);

    klog_debug("PROC", "Process reaped successfully");
}
