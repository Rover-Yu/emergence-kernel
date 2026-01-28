/* Emergence Kernel - x86-64 Interrupt Descriptor Table (IDT) */

#ifndef EMERGENCE_ARCH_X86_64_IDT_H
#define EMERGENCE_ARCH_X86_64_IDT_H

#include <stdint.h>

/* IDT Entry (64-bit gate descriptor) - 16 bytes total */
typedef struct {
    uint16_t offset_low;    /* Bits 0-15 of handler address */
    uint16_t selector;      /* Code segment selector */
    uint8_t  ist;           /* Interrupt Stack Table index (0 for none) */
    uint8_t  type_attr;     /* Type and attributes */
    uint16_t offset_mid;    /* Bits 16-31 of handler address */
    uint32_t offset_high;   /* Bits 32-63 of handler address */
    uint32_t zero;          /* Reserved (must be 0) */
} __attribute__((packed)) idt_entry_t;

/* IDT Pointer Structure (for lidt instruction) */
typedef struct {
    uint16_t limit;         /* Size of IDT - 1 */
    uint64_t base;          /* Base address of IDT */
} __attribute__((packed)) idt_ptr_t;

/* Number of IDT entries */
#define IDT_ENTRIES 256

/* IDT Gate Types */
#define IDT_GATE_INTERRUPT  0x8E   /* Interrupt gate (present, DPL=0) */

/* Common interrupt vectors */
#define IRQ_BASE           32      /* First user IRQ vector */
#define TIMER_VECTOR       32      /* Timer interrupt vector */
#define IPI_VECTOR         33      /* IPI interrupt vector */

/* Function prototypes */

/* Initialize IDT with default handlers */
void idt_init(void);

/* Set an IDT gate entry */
void idt_set_gate(uint8_t num, uint64_t handler, uint16_t selector, uint8_t type);

/* Enable/disable interrupts */
static inline void enable_interrupts(void) {
    asm volatile ("sti");
}

static inline void disable_interrupts(void) {
    asm volatile ("cli");
}

/* Timer handler (called from ISR) */
void timer_handler(void);

#endif /* EMERGENCE_ARCH_X86_64_IDT_H */
