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

    /* Save user CR3 for memory isolation
     * We save it in R14 temporarily before pushing it */
    mov %cr3, %r14

    /* Save RCX (return RIP) and R11 (RFLAGS) for later use */
    push %rcx
    push %r11
    push %r14           /* Save user CR3 */

    /* CR3 SWITCH: Switch to kernel page tables
     * For now, we keep the same CR3 (no isolation yet)
     * TODO: Load kernel master page table when address spaces are implemented
     *
     * Future implementation:
     * mov $_kernel_pml4_phys, %r14
     * mov %r14, %cr3
     */

    /* Set up syscall arguments for C handler */
    /* C handler: void syscall_handler(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3)
     * System V AMD64 ABI: params in RDI, RSI, RDX, RCX, R8, R9
     * Syscall ABI: nr in RAX, args in RDI, RSI, RDX, R10, R8, R9
     *
     * Need to map: User RAX→RDI(nr), User RDI→RSI(a1), User RSI→RDX(a2), User RDX→RCX(a3)
     */
    mov %rdi, %r8        /* Save User RDI (arg1) */
    mov %rsi, %r9        /* Save User RSI (arg2) */
    mov %rdx, %r10       /* Save User RDX (arg3) */
    mov %rax, %rdi       /* Param 1: nr = syscall number */
    mov %r8, %rsi        /* Param 2: a1 = User RDI */
    mov %r9, %rdx        /* Param 3: a2 = User RSI */
    mov %r10, %rcx       /* Param 4: a3 = User RDX */

    call syscall_handler

    /* Restore user CR3 - switch back to user page tables
     * This ensures we return to the correct address space */
    pop %r14
    mov %r14, %cr3

    /* Restore RCX and R11 */
    pop %r11
    pop %rcx

    /* Restore user stack pointer */
    mov %r15, %rsp

    /* Restore registers (RAX contains return value) */
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbp
    pop %rbx

    /* Return from syscall using sysretq
     * sysretq returns to ring 3, loading:
     *   RIP from RCX
     *   RFLAGS from R11
     *   CS from STAR[47:32] | 3
     *   SS from STAR[47:32] + 8
     */
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
