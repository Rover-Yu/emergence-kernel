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

    /* Save current RSP to per-CPU data via GS segment
     * Offset 0 = saved_rsp in per_cpu_data_t */
    mov %rsp, %gs:0

    /* CRITICAL: Save current CR3 (unprivileged page tables) to per-CPU data
     * Offset 8 = saved_cr3 in per_cpu_data_t
     * We cannot use a register because the C handler may modify it! */
    mov %cr3, %rax
    mov %rax, %gs:8

    /* Switch to monitor (privileged) page tables */
    movabs $monitor_pml4_phys, %r9
    mov (%r9), %r9           /* Load the actual PML4 physical address */
    mov %r9, %cr3            /* Switch to monitor page tables */

    /* Switch to monitor stack (identity-mapped in monitor page tables) */
    movabs $nk_boot_stack_top, %rsp
    sub $128, %rsp           /* Reserve space on stack */

    /* Call C monitor handler (args already in rdi, rsi, rdx, rcx) */
    call monitor_call_handler

    /* Restore original CR3 (unprivileged page tables) from per-CPU data */
    mov %gs:8, %rax
    mov %rax, %cr3

    /* Restore original RSP from per-CPU data */
    mov %gs:0, %rsp

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

/* Per-CPU data is now accessed via GS segment base.
 * The per_cpu_data_t structure is defined in smp.h and allocated in smp.c.
 * Assembly offsets:
 *   GS:0  = saved_rsp
 *   GS:8  = saved_cr3
 *   GS:16 = cpu_index
 *   GS:24 = reserved
 *
 * The GS base is set by smp_set_gs_base() during CPU initialization:
 * - BSP: set in kernel_main() after cpu_id assignment
 * - APs:  set in ap_start() after cpu index allocation
 */

/* External symbols */
.global monitor_pml4_phys
.global monitor_call_handler
.global nk_boot_stack_top
