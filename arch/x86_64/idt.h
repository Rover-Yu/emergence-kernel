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
#define IDT_GATE_INTERRUPT  0x8E   /* Interrupt gate (present, DPL=0) - kernel only */
#define IDT_GATE_INTERRUPT_USER 0xEE   /* Interrupt gate (present, DPL=3) - user-accessible */

/* Common interrupt vectors */
#define IRQ_BASE           32      /* First user IRQ vector */
#define TIMER_VECTOR       32      /* Timer interrupt vector */
#define IPI_VECTOR         33      /* IPI interrupt vector */

/* Interrupt flags type for saving/restoring interrupt state
 *
 * Stores the RFLAGS register value. Bit 9 (IF flag) indicates whether
 * interrupts were enabled when the flags were saved. This allows conditional restore
 * of interrupt state in irqrestore operations.
 */
typedef unsigned long irq_flags_t;

/* Function prototypes */

/* Initialize IDT with default handlers */
void idt_init(void);

/* Set an IDT gate entry */
void idt_set_gate(uint8_t num, uint64_t handler, uint16_t selector, uint8_t type);

/* Enable/disable interrupts */

/* Forward declarations */
static inline void enable_interrupts(void);
static inline void disable_interrupts(void);

/**
 * irq_save - Save interrupt flags and optionally disable interrupts
 * @flags: Pointer to store RFLAGS
 * @disable: 0 to save only, 1 to also disable interrupts
 *
 * Saves RFLAGS register to memory. If disable is true, interrupts are disabled
 * after saving. Use with irq_restore() to restore state later.
 *
 * Returns: The saved RFLAGS value with IF flag indicating interrupt state.
 */
static inline irq_flags_t irq_save(int disable) {
    irq_flags_t flags;
    asm volatile ("pushf; pop %0" : "=rm"(flags) :: "memory");
    if (disable) {
        asm volatile ("cli");
    }
    return flags;
}

/**
 * irq_restore - Restore RFLAGS from memory
 * @flags: Previously saved RFLAGS
 *
 * Restores RFLAGS using POPF. Enables interrupts only if IF was clear when saved.
 * Uses the IF flag (bit 9) to determine previous interrupt state.
 */
static inline void irq_restore(irq_flags_t *flags) {
    /* Check IF flag (bit 9) - restore interrupts only if they were enabled */
    if (*flags & (1UL << 9)) {
        enable_interrupts();
    }
    asm volatile ("push %0; popf" : : "rm"(*flags) : "memory");
}

/* Enable/disable interrupts (legacy wrappers - use irq_save/irq_restore instead) */

/**
 * enable_interrupts - Enable interrupts
 *
 * Executes STI instruction to set IF flag, allowing maskable interrupts to fire.
 */
static inline void enable_interrupts(void) {
    asm volatile ("sti");
}

/**
 * disable_interrupts - Disable interrupts
 *
 * Executes CLI instruction to clear IF flag, masking all maskable interrupts.
 * NOTE: Use irq_save(1) to both save and disable in one operation.
 */
static inline void disable_interrupts(void) {
    asm volatile ("cli");
}

/**
 * arch_irq_save - Save interrupt flags and optionally disable interrupts
 * @disable: 0 to save only, 1 to also disable interrupts
 *
 * Saves RFLAGS register to memory. If disable is true, clears IF flag
 * to mask all maskable interrupts.
 *
 * Returns: The saved RFLAGS value with IF flag (bit 9) indicating
 * whether interrupts were enabled when saved.
 */
static inline irq_flags_t arch_irq_save(int disable) {
    irq_flags_t flags;
    asm volatile ("pushf; pop %0" : "=rm"(flags) :: "memory");
    if (disable) {
        asm volatile ("cli");
    }
    return flags;
}

/**
 * save_interrupt_flags - DEPRECATED: Use irq_save(0) instead
 *
 * This function is kept for compatibility. Prefer irq_save() which
 * clearly documents intent to save without disabling interrupts.
 */
__attribute__((deprecated))
static inline void save_interrupt_flags(irq_flags_t *flags) {
    asm volatile ("pushf; pop %0" : "=rm"(*flags) :: "memory");
}

/**
 * restore_interrupt_flags - DEPRECATED: Use irq_restore() instead
 *
 * This function is kept for compatibility. Prefer irq_restore() which
 * handles conditional interrupt enable and is more explicit.
 */
__attribute__((deprecated))
static inline void restore_interrupt_flags(irq_flags_t *flags) {
    if (*flags & (1UL << 9)) {
        enable_interrupts();
    }
    asm volatile ("push %0; popf" : : "rm"(*flags) : "memory");
}

/**
 * arch_irq_restore - Restore interrupt flags from saved value
 * @flags: Pointer to saved RFLAGS
 *
 * Restores RFLAGS using POPF. Enables interrupts only if IF was set when saved.
 * Uses the IF flag (bit 9) to determine previous interrupt state.
 */
static inline void arch_irq_restore(irq_flags_t *flags) {
    /* Check IF flag (bit 9) - restore interrupts only if they were enabled */
    if (*flags & (1UL << 9)) {
        enable_interrupts();
    }
    asm volatile ("push %0; popf" : : "rm"(*flags) : "memory");
}

/* Timer handler (called from ISR) */
void timer_handler(void);

/* Load IDT register - wrapper for LIDT instruction
 * @idt_ptr: Pointer to IDT descriptor (base + limit)
 *
 * Programs the CPU interrupt descriptor table. Must be done once during boot.
 */
static inline void arch_idt_load(const idt_ptr_t *idt_ptr) {
    asm volatile ("lidt %0" : : "m"(*idt_ptr));
}

#endif /* EMERGENCE_ARCH_X86_64_IDT_H */
