/* Emergence Kernel - x86-64 IDT implementation */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "arch/x86_64/idt.h"
#include "arch/x86_64/io.h"
#include "arch/x86_64/serial.h"
#include "kernel/monitor/monitor.h"
#include "arch/x86_64/power.h"

/* IDT array - 256 entries for x86-64 */
static idt_entry_t idt[IDT_ENTRIES];

/* IDT pointer for lidt instruction */
/* NOT static so AP trampoline can access it */
idt_ptr_t idt_ptr;

/* External ISR assembly wrappers */
extern void timer_isr(void);
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

/* PIC (8259 Programmable Interrupt Controller) I/O ports */
#define PIC1_CMD  0x20   /* Master PIC command port */
#define PIC1_DATA 0x21   /* Master PIC data port */
#define PIC2_CMD  0xA0   /* Slave PIC command port */
#define PIC2_DATA 0xA1   /* Slave PIC data port */

#define ICW1_ICW4 0x01   /* ICW4 (not) needed */
#define ICW1_INIT 0x10   /* Initialization - required! */

/**
 * pic_remap - Remap PIC interrupt vectors
 *
 * Remaps the PIC interrupts from IRQ 0-7 to vectors 32-39
 * and IRQ 8-15 to vectors 40-47. This avoids conflicts with
 * the x86 exceptions (vectors 0-31).
 */
static void pic_remap(void) {
    /* Save current interrupt masks */
    uint8_t a1 = inb(PIC1_DATA);
    uint8_t a2 = inb(PIC2_DATA);

    /* Start initialization sequence (ICW1) */
    outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4);
    outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4);

    /* Set vector offsets (ICW2)
     * Master PIC: IRQ 0-7 -> vectors 32-39
     * Slave PIC: IRQ 8-15 -> vectors 40-47 */
    outb(PIC1_DATA, 0x20);   /* Master PIC vector offset */
    outb(PIC2_DATA, 0x28);   /* Slave PIC vector offset (32 + 8 = 40) */

    /* Configure slave PIC identity (ICW3)
     * Master PIC: slave at IRQ 2
     * Slave PIC: slave identity code 2 */
    outb(PIC1_DATA, 0x04);   /* Master PIC: slave at IRQ 2 */
    outb(PIC2_DATA, 0x02);   /* Slave PIC: identity code 2 */

    /* Set 8086 mode (ICW4) */
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);

    /* Restore interrupt masks */
    outb(PIC1_DATA, a1);
    outb(PIC2_DATA, a2);
}

/**
 * idt_init - Initialize IDT with default handlers
 *
 * Sets up the IDT with exception handlers and interrupt gates.
 * Must be called before enabling interrupts.
 */
void idt_init(void) {
    /* Remap PIC interrupts to avoid conflicts with exceptions */
    pic_remap();

    /* Mask all PIC interrupts to prevent spurious interrupts */
    outb(PIC1_DATA, 0xFF);  /* Mask all IRQs on master PIC */
    outb(PIC2_DATA, 0xFF);  /* Mask all IRQs on slave PIC */

    /* Clear IDT */
    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt_set_gate(i, 0, 0, 0);
    }

    /* Set up exception handlers (0-31) */
    /* Use kernel code segment (selector 0x08) */
    uint16_t kernel_cs = 0x08;

    /* IMPORTANT: Exception handlers MUST be user-accessible (DPL=3)
     * Otherwise, when an exception occurs in ring 3, the CPU will #GP trying
     * to call the handler, leading to double fault and triple fault.
     * Use IDT_GATE_INTERRUPT_USER (0xEE) for all exception handlers. */

    idt_set_gate(0, (uint64_t)divide_error_isr, kernel_cs, IDT_GATE_INTERRUPT_USER);
    idt_set_gate(1, (uint64_t)debug_isr, kernel_cs, IDT_GATE_INTERRUPT_USER);
    idt_set_gate(2, (uint64_t)nmi_isr, kernel_cs, IDT_GATE_INTERRUPT_USER);
    idt_set_gate(3, (uint64_t)breakpoint_isr, kernel_cs, IDT_GATE_INTERRUPT_USER);
    idt_set_gate(4, (uint64_t)overflow_isr, kernel_cs, IDT_GATE_INTERRUPT_USER);
    idt_set_gate(5, (uint64_t)bound_isr, kernel_cs, IDT_GATE_INTERRUPT_USER);
    idt_set_gate(6, (uint64_t)invalid_op_isr, kernel_cs, IDT_GATE_INTERRUPT_USER);
    idt_set_gate(7, (uint64_t)device_not_available_isr, kernel_cs, IDT_GATE_INTERRUPT_USER);
    idt_set_gate(8, (uint64_t)double_fault_isr, kernel_cs, IDT_GATE_INTERRUPT_USER);
    idt_set_gate(10, (uint64_t)invalid_tss_isr, kernel_cs, IDT_GATE_INTERRUPT_USER);
    idt_set_gate(11, (uint64_t)segment_not_present_isr, kernel_cs, IDT_GATE_INTERRUPT_USER);
    idt_set_gate(12, (uint64_t)stack_isr, kernel_cs, IDT_GATE_INTERRUPT_USER);
    idt_set_gate(13, (uint64_t)general_protection_isr, kernel_cs, IDT_GATE_INTERRUPT_USER);
    idt_set_gate(14, (uint64_t)page_fault_isr, kernel_cs, IDT_GATE_INTERRUPT_USER);
    idt_set_gate(15, 0, kernel_cs, IDT_GATE_INTERRUPT_USER);  /* Reserved */
    idt_set_gate(16, (uint64_t)x87_fpu_isr, kernel_cs, IDT_GATE_INTERRUPT_USER);
    idt_set_gate(17, (uint64_t)alignment_isr, kernel_cs, IDT_GATE_INTERRUPT_USER);
    idt_set_gate(18, (uint64_t)machine_check_isr, kernel_cs, IDT_GATE_INTERRUPT_USER);
    idt_set_gate(19, (uint64_t)simd_isr, kernel_cs, IDT_GATE_INTERRUPT_USER);

    /* Set up interrupt handlers (32+) */
    idt_set_gate(TIMER_VECTOR, (uint64_t)timer_isr, kernel_cs, IDT_GATE_INTERRUPT);
    /* Vector 33 is for IPI (Inter-Processor Interrupt) */
    idt_set_gate(33, (uint64_t)ipi_isr, kernel_cs, IDT_GATE_INTERRUPT);  /* IPI */
    /* More interrupt handlers can be added here */

    /* Set up IDT pointer */
    idt_ptr.limit = (sizeof(idt_entry_t) * IDT_ENTRIES) - 1;
    idt_ptr.base = (uint64_t)&idt;

    /* Load IDT using lidt instruction */
    asm volatile ("lidt %0" : : "m"(idt_ptr));
}

/* External monitor variable - used to verify unprivileged mode */
extern uint64_t unpriv_pml4_phys;

/**
 * page_fault_handler - Handle page fault with logging from outer kernel
 * @fault_addr: Faulting virtual address from CR2
 * @error_code: Page fault error code from stack
 * @fault_ip: Instruction pointer where fault occurred
 *
 * NOTE: This handler runs in outer kernel mode (unprivileged).
 * It logs the fault and initiates shutdown directly without switching
 * to monitor mode, since system_shutdown() is callable from outer kernel.
 *
 * IMPORTANT: This handler must be very simple to avoid causing additional
 * faults (double faults). Avoid complex functions that might fault.
 */
void page_fault_handler(uint64_t fault_addr, uint64_t error_code, uint64_t fault_ip) {
    /* Very simple fault handler - minimal processing to avoid double faults
     * Just call system_shutdown() which will print the shutdown message
     * and cleanly shut down the system */

    /* Shutdown directly from outer kernel */
    extern void system_shutdown(void);
    system_shutdown();

    /* Should never reach here */
    asm volatile ("cli; hlt");
    while (1) asm volatile ("hlt");
}
