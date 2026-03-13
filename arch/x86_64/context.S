/* Emergence Kernel - x86_64 Context Switch Assembly
 *
 * This file implements low-level context switching for threads.
 *
 * context_switch(prev, next) - Switch from prev thread to next thread
 * thread_entry_wrapper - Entry point for newly created threads
 *
 * Register save order must match cpu_context_t layout in thread.h exactly!
 */

.section .text

/* ============================================================================
 * context_switch - Switch CPU context from one thread to another
 *
 * C prototype: void context_switch(cpu_context_t *prev, cpu_context_t *next)
 *
 * Arguments:
 *   %rdi = prev (pointer to previous thread's cpu_context)
 *   %rsi = next (pointer to next thread's cpu_context)
 *
 * Context layout (must match kernel/thread.h cpu_context_t):
 *   Offset 0:   r15     Offset 72:  rdi
 *   Offset 8:   r14     Offset 80:  rsi
 *   Offset 16:  r13     Offset 88:  rdx
 *   Offset 24:  r12     Offset 96:  rcx
 *   Offset 32:  r11     Offset 104: rbx
 *   Offset 40:  r10     Offset 112: rax
 *   Offset 48:  r9      Offset 120: rip
 *   Offset 56:  r8      Offset 128: rsp
 *   Offset 64:  rbp     Offset 136: rflags
 * ============================================================================
 */
.global context_switch
.type context_switch, @function
context_switch:
    /* ====================================================================
     * Part 1: Save current context to prev (%rdi)
     * ==================================================================== */

    /* Save callee-saved registers first */
    mov %r15, 0(%rdi)
    mov %r14, 8(%rdi)
    mov %r13, 16(%rdi)
    mov %r12, 24(%rdi)

    /* Save caller-saved registers */
    mov %r11, 32(%rdi)
    mov %r10, 40(%rdi)
    mov %r9, 48(%rdi)
    mov %r8, 56(%rdi)

    /* Save frame pointer and argument registers */
    mov %rbp, 64(%rdi)
    mov %rdi, 72(%rdi)      /* Save original rdi before we clobbered it */
    mov %rsi, 80(%rdi)
    mov %rdx, 88(%rdi)
    mov %rcx, 96(%rdi)
    mov %rbx, 104(%rdi)
    mov %rax, 112(%rdi)

    /* Save return address as RIP
     * When this function was called, the return address was pushed onto stack.
     * It's now at (%rsp). We save it so when we switch back to this context,
     * we'll return to the caller of context_switch(). */
    mov (%rsp), %rax
    mov %rax, 120(%rdi)

    /* Save stack pointer
     * After the call instruction pushed the return address, RSP points to it.
     * The saved RSP should point to where the return address is stored,
     * so when we restore, we pop the return address and continue. */
    lea 8(%rsp), %rax       /* RSP after we would have popped return address */
    mov %rax, 128(%rdi)

    /* Save RFLAGS */
    pushfq
    pop %rax
    mov %rax, 136(%rdi)

    /* ====================================================================
     * Part 2: Restore context from next (%rsi)
     * ==================================================================== */

    /* Restore callee-saved registers */
    mov 0(%rsi), %r15
    mov 8(%rsi), %r14
    mov 16(%rsi), %r13
    mov 24(%rsi), %r12

    /* Restore caller-saved registers */
    mov 32(%rsi), %r11
    mov 40(%rsi), %r10
    mov 48(%rsi), %r9
    mov 56(%rsi), %r8

    /* Restore frame pointer */
    mov 64(%rsi), %rbp

    /* Restore general purpose registers */
    mov 88(%rsi), %rdx
    mov 96(%rsi), %rcx
    mov 104(%rsi), %rbx
    mov 112(%rsi), %rax

    /* Restore RFLAGS (before rdi/rsi since pushfq affects stack) */
    pushq 136(%rsi)
    popfq

    /* Restore argument registers (last, since we still need rsi) */
    mov 72(%rsi), %rdi
    mov 80(%rsi), %rsi      /* This must be last! */

    /* Restore stack pointer */
    mov -8(%rsi), %rsp      /* Adjust for the 8 we added when saving */

    /* Jump to the saved return address in the new context */
    ret
.size context_switch, . - context_switch


/* ============================================================================
 * thread_entry_wrapper - Entry point for newly created threads
 *
 * This is where new threads start executing. It sets up the proper
 * environment and calls the thread's entry function.
 *
 * When a thread is first created, its context.rip points here.
 * The context is set up so that:
 *   - %rsp points to the top of the thread's kernel stack
 *   - The thread's entry_func and entry_arg are in the thread_t structure
 *
 * Per-CPU data offsets (must match smp.h):
 *   Offset 48: current_thread
 *
 * Thread structure offsets (must match thread.h):
 *   Offset 216: entry_func
 *   Offset 224: entry_arg
 * ============================================================================
 */
.global thread_entry_wrapper
.type thread_entry_wrapper, @function
thread_entry_wrapper:
    /* Get current thread pointer from per-CPU data */
    mov %gs:48, %rdi        /* %rdi = current_thread */

    /* Get entry function and argument from thread structure */
    mov 216(%rdi), %rax     /* %rax = entry_func */
    mov 224(%rdi), %rdi     /* %rdi = entry_arg */

    /* Call the thread's entry function */
    call *%rax

    /* If the thread function returns, call thread_exit */
    call thread_exit

    /* Should never reach here */
1:
    hlt
    jmp 1b
.size thread_entry_wrapper, . - thread_entry_wrapper
