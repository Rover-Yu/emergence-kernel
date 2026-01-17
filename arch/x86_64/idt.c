/* JAKernel - x86-64 IDT implementation */

#include <stdint.h>
#include <stddef.h>
#include "arch/x86_64/idt.h"
#include "arch/x86_64/io.h"

/* IDT array - 256 entries for x86-64 */
static idt_entry_t idt[IDT_ENTRIES];

/* IDT pointer for lidt instruction */
static idt_ptr_t idt_ptr;

/* External ISR assembly wrappers */
extern void timer_isr(void);
extern void rtc_isr(void);
extern void ipi_isr(void);
extern void divide_error_isr(void);
extern void debug_isr(void);
extern void nmi_isr(void);
extern void breakpoint_isr(void);
extern void overflow_isr(void);
extern void bound_isr(void);
extern void invalid_op_isr(void);
extern void device_not_available_isr(void);
extern void double_fault_isr(void);
extern void invalid_tss_isr(void);
extern void segment_not_present_isr(void);
extern void stack_isr(void);
extern void general_protection_isr(void);
extern void page_fault_isr(void);
extern void x87_fpu_isr(void);
extern void alignment_isr(void);
extern void machine_check_isr(void);
extern void simd_isr(void);

/**
 * idt_set_gate - Set an IDT entry
 * @num: IDT entry number (0-255)
 * @handler: Address of interrupt handler
 * @selector: Code segment selector
 * @type: Gate type (interrupt, trap, etc.)
 */
void idt_set_gate(uint8_t num, uint64_t handler, uint16_t selector, uint8_t type) {
    if (num >= (IDT_ENTRIES - 1)) {  /* Prevent underflow warning */
        return;
    }

    idt[num].offset_low = handler & 0xFFFF;
    idt[num].offset_mid = (handler >> 16) & 0xFFFF;
    idt[num].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[num].selector = selector;
    idt[num].ist = 0;           /* No IST */
    idt[num].type_attr = type;
    idt[num].zero = 0;
}

/**
 * idt_init - Initialize IDT with default handlers
 *
 * Sets up the IDT with exception handlers and interrupt gates.
 * Must be called before enabling interrupts.
 */
void idt_init(void) {
    /* Clear IDT */
    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt_set_gate(i, 0, 0, 0);
    }

    /* Set up exception handlers (0-31) */
    /* Use kernel code segment (selector 0x08) */
    uint16_t kernel_cs = 0x08;

    idt_set_gate(0, (uint64_t)divide_error_isr, kernel_cs, IDT_GATE_INTERRUPT);
    idt_set_gate(1, (uint64_t)debug_isr, kernel_cs, IDT_GATE_INTERRUPT);
    idt_set_gate(2, (uint64_t)nmi_isr, kernel_cs, IDT_GATE_INTERRUPT);
    idt_set_gate(3, (uint64_t)breakpoint_isr, kernel_cs, IDT_GATE_INTERRUPT);
    idt_set_gate(4, (uint64_t)overflow_isr, kernel_cs, IDT_GATE_INTERRUPT);
    idt_set_gate(5, (uint64_t)bound_isr, kernel_cs, IDT_GATE_INTERRUPT);
    idt_set_gate(6, (uint64_t)invalid_op_isr, kernel_cs, IDT_GATE_INTERRUPT);
    idt_set_gate(7, (uint64_t)device_not_available_isr, kernel_cs, IDT_GATE_INTERRUPT);
    idt_set_gate(8, (uint64_t)double_fault_isr, kernel_cs, IDT_GATE_INTERRUPT);
    idt_set_gate(10, (uint64_t)invalid_tss_isr, kernel_cs, IDT_GATE_INTERRUPT);
    idt_set_gate(11, (uint64_t)segment_not_present_isr, kernel_cs, IDT_GATE_INTERRUPT);
    idt_set_gate(12, (uint64_t)stack_isr, kernel_cs, IDT_GATE_INTERRUPT);
    idt_set_gate(13, (uint64_t)general_protection_isr, kernel_cs, IDT_GATE_INTERRUPT);
    idt_set_gate(14, (uint64_t)page_fault_isr, kernel_cs, IDT_GATE_INTERRUPT);
    idt_set_gate(16, (uint64_t)x87_fpu_isr, kernel_cs, IDT_GATE_INTERRUPT);
    idt_set_gate(17, (uint64_t)alignment_isr, kernel_cs, IDT_GATE_INTERRUPT);
    idt_set_gate(18, (uint64_t)machine_check_isr, kernel_cs, IDT_GATE_INTERRUPT);
    idt_set_gate(19, (uint64_t)simd_isr, kernel_cs, IDT_GATE_INTERRUPT);

    /* Set up interrupt handlers (32+) */
    idt_set_gate(TIMER_VECTOR, (uint64_t)timer_isr, kernel_cs, IDT_GATE_INTERRUPT);
    /* Vector 33 is for IPI (Inter-Processor Interrupt) */
    idt_set_gate(33, (uint64_t)ipi_isr, kernel_cs, IDT_GATE_INTERRUPT);  /* IPI */
    /* IRQ 8 is RTC (Real Time Clock) */
    idt_set_gate(40, (uint64_t)rtc_isr, kernel_cs, IDT_GATE_INTERRUPT);  /* IRQ 8 */
    /* More interrupt handlers can be added here */

    /* Set up IDT pointer */
    idt_ptr.limit = (sizeof(idt_entry_t) * IDT_ENTRIES) - 1;
    idt_ptr.base = (uint64_t)&idt;

    /* Load IDT using lidt instruction */
    asm volatile ("lidt %0" : : "m"(idt_ptr));
}
