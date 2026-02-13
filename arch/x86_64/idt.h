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

/* External SMP function for getting interrupt nesting depth */
extern int* smp_get_irq_nest_depth_ptr(void);

/* ============================================================================
 * Raw Hardware Operations (use irq_disable/irq_enable for nested-safe)
 * ============================================================================ */

/**
 * enable_interrupts_raw - Enable interrupts (raw hardware operation)
 *
 * Executes STI instruction to set IF flag, allowing maskable interrupts to fire.
 * This is the raw hardware operation - use irq_enable() for nested-safe behavior.
 */
static inline void enable_interrupts_raw(void) {
    asm volatile ("sti");
}

/**
 * disable_interrupts_raw - Disable interrupts (raw hardware operation)
 *
 * Executes CLI instruction to clear IF flag, masking all maskable interrupts.
 * This is the raw hardware operation - use irq_disable() for nested-safe behavior.
 */
static inline void disable_interrupts_raw(void) {
    asm volatile ("cli");
}

/* ============================================================================
 * Nested-Safe Interrupt Control
 * ============================================================================ */

/**
 * irq_disable - Disable interrupts with nesting support
 *
 * Increments per-CPU nesting counter. Disables interrupts on first call.
 * Safe to call multiple times - must be matched with irq_enable().
 */
static inline void irq_disable(void) {
    int *depth_ptr = smp_get_irq_nest_depth_ptr();
    if (depth_ptr && (*depth_ptr)++ == 0) {
        disable_interrupts_raw();
    }
}

/**
 * irq_enable - Enable interrupts with nesting support
 *
 * Decrements per-CPU nesting counter. Enables interrupts only when
 * counter reaches zero (all nested disables matched).
 */
static inline void irq_enable(void) {
    int *depth_ptr = smp_get_irq_nest_depth_ptr();
    if (depth_ptr && --(*depth_ptr) == 0) {
        enable_interrupts_raw();
    }
}

/* ============================================================================
 * Interrupt Save/Restore (Scalar Flags API)
 * ============================================================================ */

/**
 * irq_save - Save interrupt flags and optionally disable interrupts
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
        disable_interrupts_raw();
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
    /* Only enable interrupts if they were enabled when saved AND no nesting remains */
    if ((flags & (1UL << 9)) && (*smp_get_irq_nest_depth_ptr() == 0)) {
        enable_interrupts_raw();
    }
    asm volatile ("push %0; popf" : : "rm"(flags) : "memory");
}

/* ============================================================================
 * Legacy Wrappers (Deprecated)
 * ============================================================================ */

__attribute__((deprecated("Use irq_disable() for nested-safe operation")))
static inline void disable_interrupts(void) {
    disable_interrupts_raw();
}

__attribute__((deprecated("Use irq_enable() for nested-safe operation")))
static inline void enable_interrupts(void) {
    enable_interrupts_raw();
}

__attribute__((deprecated("Use irq_save() instead")))
static inline void save_interrupt_flags(irq_flags_t *flags) {
    asm volatile ("pushf; pop %0" : "=rm"(*flags) :: "memory");
}

__attribute__((deprecated("Use irq_restore() with scalar value instead")))
static inline void restore_interrupt_flags(irq_flags_t *flags) {
    asm volatile ("push %0; popf" : : "rm"(*flags) : "memory");
}

/* ============================================================================
 * Architecture-Specific Aliases (Raw Operations)
 * ============================================================================ */

/**
 * arch_irq_save_raw - Save interrupt flags and optionally disable (raw)
 * @disable: 0 to save only, 1 to also disable interrupts
 *
 * Raw hardware operation without nesting support.
 */
static inline irq_flags_t arch_irq_save_raw(int disable) {
    irq_flags_t flags;
    asm volatile ("pushf; pop %0" : "=rm"(flags) :: "memory");
    if (disable) {
        disable_interrupts_raw();
    }
    return flags;
}

/**
 * arch_irq_restore_raw - Restore interrupt flags (raw operation)
 * @flags: Previously saved RFLAGS
 *
 * Raw hardware operation without nesting awareness.
 * Use irq_restore() for nested-safe behavior.
 */
static inline void arch_irq_restore_raw(irq_flags_t flags) {
    asm volatile ("push %0; popf" : : "rm"(flags) : "memory");
}

/* ============================================================================
 * Legacy Architecture-Specific (Deprecated)
 * ============================================================================ */

__attribute__((deprecated("Use irq_save() instead")))
static inline irq_flags_t arch_irq_save(int disable) {
    return arch_irq_save_raw(disable);
}

__attribute__((deprecated("Use irq_restore() with scalar value instead")))
static inline void arch_irq_restore(irq_flags_t *flags) {
    if (*flags & (1UL << 9)) {
        enable_interrupts_raw();
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
