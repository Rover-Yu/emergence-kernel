/* monitor_call.S - Monitor call assembly stub for CR3 switching */
/* This stub switches from unprivileged to privileged mode to execute monitor calls */

.section .text
.global nk_entry_trampoline
.type nk_entry_trampoline, @function

/* nk_entry_trampoline - Switch to privileged mode, call handler, return
 *
 * This trampoline performs a controlled CR3 switch to enter privileged mode:
 * 1. Saves all registers and current RSP
 * 2. Saves current CR3 (unprivileged page tables)
 * 3. Switches CR3 to monitor_pml4_phys (privileged page tables)
 * 4. Switches to monitor-side stack
 * 5. Calls C monitor handler
 * 6. Restores original CR3 and RSP
 * 7. Restores all registers and returns
 *
 * Arguments:
 *   rdi = monitor_call_t call
 *   rsi = uint64_t arg1
 *   rdx = uint64_t arg2
 *   rcx = uint64_t arg3
 * Returns:
 *   rax = monitor_ret_t.result
 *   rdx = monitor_ret_t.error
 */
nk_entry_trampoline:
    /* Save all registers */
    push %rbx
    push %rbp
    push %r12
    push %r13
    push %r14
    push %r15

    /* Save current RSP to a location valid in BOTH page tables
     * Uses RIP-relative addressing so saved_rsp is at a fixed virtual
     * address that works in both page tables (trampoline is identity-mapped) */
    mov %rsp, saved_rsp(%rip)

    /* CRITICAL: Save current CR3 (unprivileged page tables) to memory
     * We cannot use a register because the C handler may modify it!
     * Per System V AMD64 ABI, r8-r11 are volatile and can be clobbered. */
    mov %cr3, %rax
    mov %rax, saved_cr3(%rip)

    /* Switch to monitor (privileged) page tables */
    movabs $monitor_pml4_phys, %r9
    mov (%r9), %r9           /* Load the actual PML4 physical address */
    mov %r9, %cr3            /* Switch to monitor page tables */

    /* Switch to monitor stack (identity-mapped in monitor page tables) */
    movabs $nk_boot_stack_top, %rsp
    sub $128, %rsp           /* Reserve space on stack */

    /* Call C monitor handler (args already in rdi, rsi, rdx, rcx) */
    call monitor_call_handler

    /* Restore original CR3 (unprivileged page tables) from memory */
    mov saved_cr3(%rip), %rax
    mov %rax, %cr3

    /* Restore original RSP */
    mov saved_rsp(%rip), %rsp

    /* Restore all registers */
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbp
    pop %rbx

    /* Return to caller */
    ret

.size nk_entry_trampoline, . - nk_entry_trampoline

/* Per-CPU saved RSP storage - must be accessible in both page tables
 * This is RIP-relative addressed from nk_entry_trampoline which is
 * identity-mapped in both page tables.
 *
 * NOTE: Currently uses a single global slot. For SMP support, each CPU
 * needs its own saved_rsp slot (e.g., via per-CPU data area). */
.section .bss
.align 8
.global saved_rsp
saved_rsp:
    .quad 0

/* Saved CR3 value - must also be accessible in both page tables
 * We save CR3 to memory because volatile registers (r8-r11) can be
 * modified by the C handler, and we need the original CR3 value after
 * the handler returns. */
.global saved_cr3
saved_cr3:
    .quad 0

/* External symbols */
.global monitor_pml4_phys
.global monitor_call_handler
.global nk_boot_stack_top
