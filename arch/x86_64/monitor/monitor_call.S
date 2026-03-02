/* monitor_call.S - Monitor call assembly stub for CR0.WP toggle */
/* This stub toggles CR0.WP to allow writes to read-only pages during monitor calls */

.section .text
.global nk_entry_trampoline
.type nk_entry_trampoline, @function

/* nk_entry_trampoline - Toggle CR0.WP, call handler, restore CR0.WP
 *
 * This trampoline uses CR0.WP toggling for privilege separation:
 * 1. Saves all registers and current RSP
 * 2. Saves current CR0.WP state to per-CPU data
 * 3. Clears CR0.WP (bit 16) to allow writes to read-only PTEs
 * 4. Switches to monitor-side stack
 * 5. Calls C monitor handler
 * 6. Restores CR0.WP state
 * 7. Restores all registers and returns
 *
 * Key insight: CR0.WP controls whether ring 0 code can write to read-only pages
 * - WP=1: Ring 0 cannot write to read-only pages (hardware enforced)
 * - WP=0: Ring 0 can write to any page (ignores R/W bit)
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
 *   GS:8  = saved_cr3 (kept for future use)
 *   GS:16 = cpu_index
 *   GS:24 = saved_rax (return value result)
 *   GS:32 = saved_rdx (return value error)
 *   GS:40 = saved_cr0 (saved CR0.WP state)
 */

nk_entry_trampoline:
    /* Save callee-saved registers */
    push %rbx
    push %rbp
    push %r12
    push %r13
    push %r14
    push %r15

    /* Save current RSP to per-CPU data via GS segment
     * Offset 0 = saved_rsp in per_cpu_data_t */
    mov %rsp, %gs:0

    /* Save current CR0.WP state to per-CPU data (offset 40)
     * We need to preserve the entire CR0 but only modify WP bit */
    mov %cr0, %r8
    mov %r8, %gs:40   /* saved_cr0 */

    /* Clear CR0.WP (bit 16) to allow NK to write to read-only pages
     * btr = bit test and reset - clears the bit and sets CF if it was 1 */
    btr $16, %r8
    mov %r8, %cr0

    /* Switch to monitor stack (use AP trampoline stack to avoid overlap) */
    movabs $nk_trampoline_stack_end, %rsp
    sub $256, %rsp           /* Reserve space for handler */

    /* Call C monitor handler (args already in rdi, rsi, rdx, rcx) */
    call monitor_call_handler

    /* Save return values to callee-saved registers that we already pushed */
    mov %rax, %r14           /* Save result in r14 */
    mov %rdx, %r15           /* Save error in r15 */

    /* Restore CR0.WP from saved state */
    mov %gs:40, %r8
    mov %r8, %cr0

    /* Restore original RSP from per-CPU data */
    mov %gs:0, %r8
    mov %r8, %rsp

    /* Restore registers and handle return values
     * Stack at this point: saved_r15, saved_r14, saved_r13, saved_r12, saved_rbp, saved_rbx, ret_addr
     * We need to:
     * 1. Pop saved r15/r14 into temporaries
     * 2. Move return values to rax/rdx
     * 3. Restore original r14/r15 from temporaries
     * 4. Pop remaining registers
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
 *   GS:8  = saved_cr3 (kept for future use, not used in this design)
 *   GS:16 = cpu_index
 *   GS:24 = saved_rax (return value result)
 *   GS:32 = saved_rdx (return value error)
 *   GS:40 = saved_cr0 (CR0.WP state for NK entry/exit)
 *
 * The GS base is set by smp_set_gs_base() during CPU initialization:
 * - BSP: set in kernel_main() after cpu_id assignment
 * - APs:  set in ap_start() after cpu index allocation
 *
 * Design note: This implementation uses a single shared page table (unpriv_pml4)
 * with CR0.WP toggle for privilege separation, eliminating the need for CR3 switches.
 * This reduces overhead and simplifies the monitor entry/exit path.
 */

/* External symbols */
.global monitor_call_handler
.global nk_trampoline_stack_end
