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
 *
 * Per-CPU data offsets (per_cpu_data_t in smp.h):
 *   GS:0  = saved_rsp
 *   GS:8  = saved_cr3
 *   GS:16 = cpu_index
 *   GS:24 = saved_rax (return value result)
 *   GS:32 = saved_rdx (return value error)
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
     * Offset 8 = saved_cr3 in per_cpu_data_t */
    mov %cr3, %r8
    mov %r8, %gs:8

    /* Switch to monitor (privileged) page tables */
    movabs $monitor_pml4_phys, %r9
    mov (%r9), %r9           /* Load the actual PML4 physical address */
    mov %r9, %cr3            /* Switch to monitor page tables */

    /* Switch to monitor stack (use AP trampoline stack to avoid overlap) */
    movabs $nk_trampoline_stack_end, %rsp
    sub $256, %rsp           /* Reserve space for handler */

    /* Call C monitor handler (args already in rdi, rsi, rdx, rcx) */
    call monitor_call_handler

    /* Save return value to callee-saved registers that we already pushed */
    mov %rax, %r14           /* Save result in r14 */
    mov %rdx, %r15           /* Save error in r15 */

    /* Restore original CR3 (unprivileged page tables) from per-CPU data */
    mov %gs:8, %r8
    mov %r8, %cr3

    /* Restore original RSP from per-CPU data */
    mov %gs:0, %r8
    mov %r8, %rsp

    /* Restore all registers (in reverse order of saves)
     * Stack at this point: saved_r15, saved_r14, saved_r13, saved_r12, saved_rbp, saved_rbx, ret_addr
     * But r14/r15 now hold return values, so we need a different approach:
     * 1. Pop saved_r15 and saved_r14 into temporary registers (r13/r12)
     * 2. Move return values to rax/rdx
     * 3. Restore original r14/r15 from temporaries
     * 4. Pop remaining registers in reverse order
     */

    /* Pop saved r15 and r14 into temporaries */
    pop %r13                 /* r13 = saved_r15 */
    pop %r12                 /* r12 = saved_r14 */

    /* Move return values to rax/rdx */
    mov %r14, %rax           /* rax = result */
    mov %r15, %rdx           /* rdx = error */

    /* Restore original r14/r15 from temporaries */
    mov %r12, %r14           /* r14 = saved_r14 */
    mov %r13, %r15           /* r15 = saved_r15 */

    /* Pop remaining registers in reverse order of saves */
    pop %r13                 /* Restore original r13 */
    pop %r12                 /* Restore original r12 */
    pop %rbp                 /* Restore original rbp */
    pop %rbx                 /* Restore original rbx */

    /* Return to caller */
    ret

.size nk_entry_trampoline, . - nk_entry_trampoline

/* Per-CPU data is accessed via GS segment base.
 * The per_cpu_data_t structure is defined in smp.h and allocated in smp.c.
 * Assembly offsets:
 *   GS:0  = saved_rsp
 *   GS:8  = saved_cr3
 *   GS:16 = cpu_index
 *   GS:24 = saved_rax (return value result)
 *   GS:32 = saved_rdx (return value error)
 *
 * The GS base is set by smp_set_gs_base() during CPU initialization:
 * - BSP: set in kernel_main() after cpu_id assignment
 * - APs:  set in ap_start() after cpu index allocation
 */

/* External symbols */
.global monitor_pml4_phys
.global monitor_call_handler
.global nk_trampoline_stack_end
