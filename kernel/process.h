/* Emergence Kernel - Process Management
 *
 * Provides process control and lifecycle management for user processes.
 * Each process has its own address space and contains one or more threads.
 */

#ifndef _KERNEL_PROCESS_H
#define _KERNEL_PROCESS_H

#include <stdint.h>
#include <stddef.h>
#include "kernel/list.h"
#include "kernel/thread.h"
#include "kernel/vm.h"
#include "include/spinlock.h"

/* Forward declaration */
typedef struct process process_t;

/* Process states */
typedef enum {
    PROCESS_CREATED,     /* Process created but not yet runnable */
    PROCESS_RUNNING,     /* Process is currently running */
    PROCESS_SLEEPING,    /* Process is sleeping (waiting) */
    PROCESS_ZOMBIE,      /* Process has exited but not yet reaped */
    PROCESS_DEAD         /* Process is being destroyed */
} process_state_t;

/* Maximum process name length */
#define PROCESS_NAME_MAX  32

/* Maximum number of open file descriptors per process */
#define PROCESS_MAX_FDS  256

/* Maximum PID value */
#define PID_MAX          32768

/**
 * struct process_fd - File descriptor entry
 * @flags: File descriptor flags (close-on-exec, etc.)
 * @refcount: Reference count for dup()
 * @file: Pointer to file structure (when VFS is implemented)
 *
 * Tracks open file descriptors for a process.
 * For now, this is a stub until full VFS is implemented.
 */
typedef struct process_fd {
    int flags;
    int refcount;
    void *file;  /* Will be struct file * when VFS exists */
} process_fd_t;

/**
 * struct process - Process Control Block
 * @pid: Process ID
 * @ppid: Parent Process ID
 * @state: Current process state
 * @exit_status: Exit status code (only valid if state == PROCESS_ZOMBIE)
 * @flags: Process flags
 * @name: Process name
 *
 * @parent: Pointer to parent process
 * @children: List of child processes
 * @siblings: List linkage for parent's children list
 * @all_list: List linkage for global process list
 *
 * @threads: List of threads in this process
 * @thread_count: Number of threads
 * @main_thread: Primary thread (for single-threaded processes)
 *
 * @vm: Virtual address space
 * @fd_table: File descriptor table
 * @fd_count: Number of open file descriptors
 *
 * @waiter: Thread waiting for this process to exit (for wait/wait4)
 * @exit_lock: Lock protecting exit-related fields
 *
 * Per-process control block containing all state needed for
 * process management, memory isolation, and resource tracking.
 */
struct process {
    /* Process identification */
    int pid;                        /* Process ID */
    int ppid;                       /* Parent Process ID */
    process_state_t state;          /* Current state */
    int exit_status;                /* Exit status code */
    int flags;                      /* Process flags */
    char name[PROCESS_NAME_MAX];    /* Process name */

    /* Process hierarchy */
    process_t *parent;              /* Parent process pointer */
    struct list_head children;      /* List of child processes */
    struct list_head siblings;      /* Linkage for parent's children list */
    struct list_head all_list;      /* Linkage for global process list */

    /* Thread management */
    struct list_head threads;       /* List of threads in this process */
    int thread_count;               /* Number of threads */
    thread_t *main_thread;          /* Primary thread */

    /* Memory management */
    address_space_t *vm;            /* Virtual address space */

    /* File I/O (stub until VFS is implemented) */
    process_fd_t fd_table[PROCESS_MAX_FDS];
    int fd_count;

    /* Wait/exit synchronization */
    thread_t *waiter;               /* Thread waiting for exit */
    spinlock_t exit_lock;           /* Lock for exit fields */
};

/* Process flags */
#define PROC_FLAG_EXITING    (1 << 0)  /* Process is exiting */
#define PROC_FLAG_PTRACED    (1 << 1)  /* Process is being traced (ptrace) */
#define PROC_FLAG_VFORK_DONE (1 << 2)  /* vfork parent has released */

/* Process API */

/**
 * process_init - Initialize the process subsystem
 *
 * Creates slab cache for process_t structures and initializes
 * the PID allocator. Must be called before any process_* operations.
 */
void process_init(void);

/**
 * process_create - Create a new process
 * @name: Process name
 * @flags: Process creation flags
 *
 * Returns: Pointer to new process, or NULL on failure
 *
 * Creates a new process with allocated PID and empty address space.
 * The process starts in CREATED state and must be activated to run.
 */
process_t *process_create(const char *name, int flags);

/**
 * process_fork - Fork the current process
 *
 * Returns: Pointer to child process (in parent), or NULL on failure
 *
 * Creates a child process that is a copy of the current process.
 * Clones the address space, creates a new thread, and adds to scheduler.
 * Returns NULL in child process (executing in new context).
 */
process_t *process_fork(void);

/**
 * process_exit - Exit the current process
 * @exit_code: Exit status code
 *
 * Terminates the current process and all its threads.
 * The process becomes a zombie until reaped by parent.
 * Does not return.
 */
void process_exit(int exit_code) __attribute__((noreturn));

/**
 * process_wait - Wait for a child process to exit
 * @pid: PID to wait for (-1 = any child)
 * @status: Pointer to store exit status
 *
 * Returns: PID of exited child, or negative error code
 *
 * Blocks the calling process until a child process exits.
 * If the child has already exited (zombie), returns immediately.
 * Reaps the zombie process and stores exit status in *status.
 */
int process_wait(int pid, int *status);

/**
 * process_reap - Reap a zombie process
 * @p: Zombie process to reap
 *
 * Frees all resources associated with the process and removes
 * it from the process table. The process_t structure is freed.
 */
void process_reap(process_t *p);

/**
 * process_get_current - Get the current process
 *
 * Returns: Pointer to current process, or NULL if no process running
 */
process_t *process_get_current(void);

/**
 * process_get_by_pid - Find a process by PID
 * @pid: Process ID to find
 *
 * Returns: Pointer to process, or NULL if not found
 */
process_t *process_get_by_pid(int pid);

/**
 * process_add_thread - Add a thread to a process
 * @p: Process
 * @t: Thread to add
 *
 * Links the thread to the process and sets up cross-references.
 */
void process_add_thread(process_t *p, thread_t *t);

/**
 * process_remove_thread - Remove a thread from a process
 * @p: Process
 * @t: Thread to remove
 *
 * Unlinks the thread from the process.
 */
void process_remove_thread(process_t *p, thread_t *t);

/**
 * process_alloc_pid - Allocate a new PID
 *
 * Returns: New PID, or negative error code on failure
 */
int process_alloc_pid(void);

/**
 * process_free_pid - Free a PID
 * @pid: PID to free
 */
void process_free_pid(int pid);

#endif /* _KERNEL_PROCESS_H */
