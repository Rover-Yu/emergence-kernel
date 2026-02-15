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

/* Interrupt flags type for saving/restoring interrupt state */
typedef unsigned long irq_flags_t;

/* External SMP function for getting interrupt nesting depth */
extern int* smp_get_irq_nest_depth_ptr(void);

/* Function prototypes */

/* Initialize IDT with default handlers */
void idt_init(void);

/* Set an IDT gate entry */
void idt_set_gate(uint8_t num, uint64_t handler, uint16_t selector, uint8_t type);

/* Load IDT pointer (lidt instruction) */
static inline void arch_idt_load(idt_ptr_t *ptr) {
    asm volatile ("lidt %0" : : "m"(*ptr));
}

/* ============================================================================
 * Raw Interrupt Control (No Nesting Tracking)
 * ============================================================================ */

/**
 * disable_interrupts_raw - Disable interrupts (raw, no nesting tracking)
 *
 * Executes CLI instruction to clear IF flag in RFLAGS.
 * Use only in early boot or when you know nesting is not active.
 */
static inline void disable_interrupts_raw(void) {
    asm volatile ("cli" : : : "memory");
}

/**
 * enable_interrupts_raw - Enable interrupts (raw, no nesting tracking)
 *
 * Executes STI instruction to set IF flag in RFLAGS.
 * Use only in early boot or when you know nesting is not active.
 */
static inline void enable_interrupts_raw(void) {
    asm volatile ("sti" : : : "memory");
}

/* Convenience aliases (kept for compatibility during transition) */
static inline void disable_interrupts(void) {
    disable_interrupts_raw();
}

static inline void enable_interrupts(void) {
    enable_interrupts_raw();
}

/* ============================================================================
 * Nested-Safe Interrupt Control
 * ============================================================================ */

/**
 * irq_disable - Disable interrupts with nesting support
 *
 * Increments nesting counter and disables interrupts.
 * Nested calls are safe - interrupts only re-enabled when
 * nesting depth returns to zero.
 */
static inline void irq_disable(void) {
    int *depth = smp_get_irq_nest_depth_ptr();
    if (depth) {
        (*depth)++;
        if (*depth == 1) {
            asm volatile ("cli" : : : "memory");
        }
    } else {
        /* Fallback: disable interrupts without nesting */
        asm volatile ("cli" : : : "memory");
    }
}

/**
 * irq_enable - Enable interrupts with nesting support
 *
 * Decrements nesting counter and enables interrupts if depth reaches zero.
 * Safe for nested calls - only actually enables at outermost level.
 */
static inline void irq_enable(void) {
    int *depth = smp_get_irq_nest_depth_ptr();
    if (depth) {
        (*depth)--;
        if (*depth == 0) {
            asm volatile ("sti" : : : "memory");
        }
    } else {
        /* Fallback: enable interrupts without nesting */
        asm volatile ("sti" : : : "memory");
    }
}

/**
 * irq_save - Save interrupt flags and optionally disable interrupts
 * @disable: Non-zero to disable interrupts, zero to only save
 *
 * Returns: Current RFLAGS value (scalar)
 *
 * Saves RFLAGS using PUSHF. If @disable is non-zero, also
 * disables interrupts with nesting support.
 */
static inline irq_flags_t irq_save(int disable) {
    irq_flags_t flags;
    asm volatile ("pushf; pop %0" : "=rm"(flags) : : "memory");
    if (disable) {
        irq_disable();
    }
    return flags;
}

/**
 * irq_restore - Restore interrupt flags from saved value
 * @flags: Previously saved RFLAGS (scalar value, not pointer)
 *
 * Restores RFLAGS using POPF. Enables interrupts only if:
 * 1) IF was set when saved (bit 9 of flags)
 * 2) No nested interrupt disables remain active
 *
 * Nesting-aware: Only enables interrupts when nesting depth is zero.
 */
static inline void irq_restore(irq_flags_t flags) {
    int *depth = smp_get_irq_nest_depth_ptr();

    /* Decrement nesting depth */
    if (depth && *depth > 0) {
        (*depth)--;
    }

    /* Only enable interrupts if they were enabled when saved AND no nesting remains */
    if (flags & (1UL << 9)) {
        if (!depth || *depth == 0) {
            asm volatile ("sti" : : : "memory");
        }
    }
}

#endif /* EMERGENCE_ARCH_X86_64_IDT_H */
