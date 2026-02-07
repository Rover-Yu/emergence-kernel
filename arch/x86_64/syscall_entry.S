#include "syscall.h"

/* External symbol for kernel stack */
.extern nk_boot_stack_top

/* External syscall handler */
.extern syscall_handler

/* Syscall entry point - called via SYSCALL instruction */
/* SYSCALL saves: RCX (return RIP), R11 (saved RFLAGS) */
/* Switches to: kernel CS (from STAR), CPL 0 */

.section .text
.global syscall_entry
syscall_entry:
    /* Save user registers */
    push %rbx
    push %rbp
    push %r12
    push %r13
    push %r14
    push %r15

    /* Save user stack pointer and switch to kernel stack */
    mov %rsp, %r15
    leaq nk_boot_stack_top(%rip), %rsp

    /* Set up syscall arguments for C handler */
    /* Syscall number in RAX, args in RDI, RSI, RDX */
    mov %rdx, %r8        /* arg3 */
    mov %rsi, %rdx       /* arg2 in RDX */
    mov %rdi, %rsi       /* arg1 in RSI */
    mov %rax, %rdi       /* syscall number in RDI */

    call syscall_handler

    /* Restore user stack pointer */
    mov %r15, %rsp

    /* Restore registers (RAX contains return value) */
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbp
    pop %rbx

    /* SYSRET returns to: RCX (user RIP), R11 (user RFLAGS) */
    /* Switches to: user CS (from STAR), CPL 3 */
    sysretq

/* Function to jump to user mode using sysretq
 * sysretq switches from ring 0 to ring 3 using STAR MSR.
 *
 * Arguments: (in registers)
 *   RDI = user RIP
 *   RSI = user RSP
 *   RDX = user RFLAGS
 * Clobbers: RAX, RCX, R11
 */
.global jump_to_user_mode
jump_to_user_mode:
    /* Jump to user mode using sysretq
     * Arguments: RDI=user RIP, RSI=user RSP, RDX=user RFLAGS
     */

    /* Ensure RCX is even (sysretq requires RCX[0] = 0) */
    mov %rdi, %rcx
    and $~1, %rcx               /* Clear bit 0 */

    /* Set up for sysretq */
    mov %rdx, %r11              /* R11 = user RFLAGS */

    /* CRITICAL: sysretq does NOT switch RSP automatically!
     * We need to manually switch to user stack BEFORE sysretq */
    mov %rsi, %rsp              /* Switch to user stack */

    /* sysretq switches to user mode */
    sysretq

    /* Should never reach here */
    ud2
