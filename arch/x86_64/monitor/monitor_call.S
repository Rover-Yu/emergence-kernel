/* monitor_call.S - Monitor call assembly stub for CR3 switching */
/* This stub switches from unprivileged to privileged mode to execute monitor calls */

.section .text
.global monitor_call_stub
.type monitor_call_stub, @function

/* monitor_call_stub - Switch to privileged mode and execute monitor call
 * Arguments:
 *   rdi = monitor_call_t call
 *   rsi = uint64_t arg1
 *   rdx = uint64_t arg2
 *   rcx = uint64_t arg3
 * Returns:
 *   rax = monitor_ret_t.result
 *   rdx = monitor_ret_t.error
 */
monitor_call_stub:
    /* Save all registers */
    push %rbx
    push %rbp
    push %r12
    push %r13
    push %r14
    push %r15

    /* Save current RSP and CR3 */
    mov %rsp, %r8
    mov %cr3, %r9

    /* Switch to privileged page tables */
    mov monitor_pml4_phys(%rip), %r10
    mov %r10, %cr3

    /* Call C monitor handler */
    mov %rdi, %rdi       /* call */
    mov %rsi, %rsi       /* arg1 */
    mov %rdx, %rdx       /* arg2 */
    mov %rcx, %rcx       /* arg3 */
    call monitor_call_handler

    /* Restore unprivileged CR3 */
    mov %r9, %cr3

    /* Restore RSP */
    mov %r8, %rsp

    /* Restore registers */
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbp
    pop %rbx

    ret

.size monitor_call_stub, . - monitor_call_stub

/* External symbols */
.global monitor_pml4_phys
.global monitor_call_handler
