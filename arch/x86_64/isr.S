/* JAKernel - x86-64 Interrupt Service Routine (ISR) wrappers */

.section .text

/* Macro to define ISR stub
 * This macro saves registers, calls the C handler, and restores registers */
.macro ISR_HANDLER name
.global \name
.type \name, @function
\name:
    /* Save all general-purpose registers (x86-64 has no pusha) */
    push %r15
    push %r14
    push %r13
    push %r12
    push %r11
    push %r10
    push %r9
    push %r8
    push %rbp
    push %rdi
    push %rsi
    push %rdx
    push %rcx
    push %rbx
    push %rax

    /* In x86-64 long mode, segment registers are fixed to flat model
     * We don't need to save/restore %ds, %es, %fs, %gs */

    /* Call C handler */
    call rtc_isr_handler

    /* Call C handler */
    call \name##_handler

    /* Restore data segment */
    pop %rax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    /* Restore all general-purpose registers */
    pop %rax
    pop %rbx
    pop %rcx
    pop %rdx
    pop %rsi
    pop %rdi
    pop %rbp
    pop %r8
    pop %r9
    pop %r10
    pop %r11
    pop %r12
    pop %r13
    pop %r14
    pop %r15

    /* Return from interrupt */
    iretq
.size \name, . - \name
.endm

/* ============================================
 * Exception ISRs (0-31)
 * ============================================ */

ISR_HANDLER divide_error_isr
ISR_HANDLER debug_isr
ISR_HANDLER nmi_isr
ISR_HANDLER breakpoint_isr
ISR_HANDLER overflow_isr
ISR_HANDLER bound_isr
ISR_HANDLER invalid_op_isr
ISR_HANDLER device_not_available_isr
ISR_HANDLER double_fault_isr
ISR_HANDLER invalid_tss_isr
ISR_HANDLER segment_not_present_isr
ISR_HANDLER stack_isr
ISR_HANDLER general_protection_isr
ISR_HANDLER page_fault_isr
ISR_HANDLER x87_fpu_isr
ISR_HANDLER alignment_isr
ISR_HANDLER machine_check_isr
ISR_HANDLER simd_isr

/* ============================================
 * Interrupt ISRs (32+)
 * ============================================ */

/* Timer ISR - special handling for APIC EOI */
.global timer_isr
.type timer_isr, @function
timer_isr:
    /* Save all general-purpose registers (x86-64 has no pusha)
     * Save in order: r15 first, rax last (reverse restore) */
    push %r15
    push %r14
    push %r13
    push %r12
    push %r11
    push %r10
    push %r9
    push %r8
    push %rbp
    push %rdi
    push %rsi
    push %rdx
    push %rcx
    push %rbx
    push %rax

    /* In x86-64 long mode, segment registers are fixed to flat model
     * We don't need to save/restore %ds, %es, %fs, %gs */

    /* Call C handler */
    call timer_handler

    /* Send EOI to Local APIC */
    /* LAPIC EOI is at offset 0xB0 from base (0xFEE00000) */
    movabs $0xFEE000B0, %rax      /* Load absolute EOI address */
    movl $0, (%rax)              /* Write 0 to EOI register */

    /* Restore all general-purpose registers
     * Restore in reverse order: r15 first, rax last */
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rbp
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rbx
    pop %rax

    /* Return from interrupt */
    iretq
.size timer_isr, . - timer_isr

/* RTC ISR - Real Time Clock for periodic interrupts */
.global rtc_isr
.type rtc_isr, @function
rtc_isr:
    /* Save all general-purpose registers (x86-64 has no pusha)
     * Save in order: r15 first, rax last (reverse restore) */
    push %r15
    push %r14
    push %r13
    push %r12
    push %r11
    push %r10
    push %r9
    push %r8
    push %rbp
    push %rdi
    push %rsi
    push %rdx
    push %rcx
    push %rbx
    push %rax

    /* In x86-64 long mode, segment registers are fixed to flat model
     * We don't need to save/restore %ds, %es, %fs, %gs */

    /* Call C handler */
    call rtc_isr_handler

    /* Restore all general-purpose registers
     * Restore in reverse order: r15 first, rax last */
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rbp
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rbx
    pop %rax

    /* Return from interrupt */
    iretq
.size rtc_isr, . - rtc_isr

/* IPI ISR - Inter-Processor Interrupt */
.global ipi_isr
.type ipi_isr, @function
ipi_isr:
    /* Save all general-purpose registers (x86-64 has no pusha)
     * Save in order: r15 first, rax last (reverse restore) */
    push %r15
    push %r14
    push %r13
    push %r12
    push %r11
    push %r10
    push %r9
    push %r8
    push %rbp
    push %rdi
    push %rsi
    push %rdx
    push %rcx
    push %rbx
    push %rax

    /* In x86-64 long mode, segment registers are fixed to flat model
     * We don't need to save/restore %ds, %es, %fs, %gs */

    /* Call C handler */
    call ipi_isr_handler

    /* Restore all general-purpose registers
     * Restore in reverse order: r15 first, rax last */
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rbp
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rbx
    pop %rax

    /* Return from interrupt */
    iretq
.size ipi_isr, . - ipi_isr



/* ============================================
 * Default ISR stubs for unhandled interrupts
 * ============================================ */

.global irq_common_stub
irq_common_stub:
    /* This should never be called */
    cli
1:
    hlt
    jmp 1b

/* ============================================
 * C handler stubs for exceptions
 * These just halt for now - proper exception handling can be added later
 * ============================================ */

.section .text
.global divide_error_isr_handler
divide_error_isr_handler:
    cli
    hlt
    jmp divide_error_isr_handler

.global debug_isr_handler
debug_isr_handler:
    cli
    hlt
    jmp debug_isr_handler

.global nmi_isr_handler
nmi_isr_handler:
    iretq

.global breakpoint_isr_handler
breakpoint_isr_handler:
    cli
    hlt
    jmp breakpoint_isr_handler

.global overflow_isr_handler
overflow_isr_handler:
    cli
    hlt
    jmp overflow_isr_handler

.global bound_isr_handler
bound_isr_handler:
    cli
    hlt
    jmp bound_isr_handler

.global invalid_op_isr_handler
invalid_op_isr_handler:
    cli
    hlt
    jmp invalid_op_isr_handler

.global device_not_available_isr_handler
device_not_available_isr_handler:
    cli
    hlt
    jmp device_not_available_isr_handler

.global double_fault_isr_handler
double_fault_isr_handler:
    cli
    hlt
    jmp double_fault_isr_handler

.global invalid_tss_isr_handler
invalid_tss_isr_handler:
    cli
    hlt
    jmp invalid_tss_isr_handler

.global segment_not_present_isr_handler
segment_not_present_isr_handler:
    cli
    hlt
    jmp segment_not_present_isr_handler

.global stack_isr_handler
stack_isr_handler:
    cli
    hlt
    jmp stack_isr_handler

.global general_protection_isr_handler
general_protection_isr_handler:
    cli
    hlt
    jmp general_protection_isr_handler

.global page_fault_isr_handler
page_fault_isr_handler:
    cli
    hlt
    jmp page_fault_isr_handler

.global x87_fpu_isr_handler
x87_fpu_isr_handler:
    iretq

.global alignment_isr_handler
alignment_isr_handler:
    cli
    hlt
    jmp alignment_isr_handler

.global machine_check_isr_handler
machine_check_isr_handler:
    cli
    hlt
    jmp machine_check_isr_handler

.global simd_isr_handler
simd_isr_handler:
    iretq
