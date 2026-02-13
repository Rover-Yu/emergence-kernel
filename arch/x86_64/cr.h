/* Emergence Kernel - x86-64 Control Register (CR) Access */

#ifndef EMERGENCE_ARCH_X86_64_CR_H
#define EMERGENCE_ARCH_X86_64_CR_H

#include <stdint.h>

/**
 * arch_cr0_read - Read CR0 register
 *
 * Returns: Current CR0 value
 *
 * CR0 contains control flags: PE, MP, EM, TS, ET, NE, WP, AM, WP, NE, etc.
 */
static inline uint64_t arch_cr0_read(void) {
    uint64_t cr0;
    asm volatile ("mov %%cr0, %0" : "=r"(cr0));
    return cr0;
}

/**
 * arch_cr0_write - Write CR0 register
 * @value: Value to write to CR0
 *
 * CR0 write enables/disables paging, protection, etc.
 * Be extremely careful - writing wrong values can crash the system.
 */
static inline void arch_cr0_write(uint64_t value) {
    asm volatile ("mov %0, %%cr0" : : "r"(value) : "memory");
}

/**
 * arch_cr0_set_bit - Set a bit in CR0
 * @bit: Bit number to set (0-63)
 *
 * Reads current CR0, sets the bit, writes back.
 */
static inline void arch_cr0_set_bit(uint8_t bit) {
    uint64_t cr0 = arch_cr0_read();
    cr0 |= (1ULL << bit);
    arch_cr0_write(cr0);
}

/**
 * arch_cr0_clear_bit - Clear a bit in CR0
 * @bit: Bit number to clear (0-63)
 *
 * Reads current CR0, clears the bit, writes back.
 */
static inline void arch_cr0_clear_bit(uint8_t bit) {
    uint64_t cr0 = arch_cr0_read();
    cr0 &= ~(1ULL << bit);
    arch_cr0_write(cr0);
}

/**
 * arch_cr3_read - Read CR3 register
 *
 * Returns: Current CR3 value (page table base address)
 *
 * CR3 contains the physical address of the root page table (PML4).
 */
static inline uint64_t arch_cr3_read(void) {
    uint64_t cr3;
    asm volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

/**
 * arch_cr3_write - Write CR3 register
 * @value: Value to write to CR3
 *
 * Sets new page table base. Flushes TLB automatically on x86-64.
 * Lower 12 bits must be 0 (page-aligned).
 */
static inline void arch_cr3_write(uint64_t value) {
    asm volatile ("mov %0, %%cr3" : : "r"(value) : "memory");
}

/**
 * arch_cr4_read - Read CR4 register
 *
 * Returns: Current CR4 value
 *
 * CR4 contains feature enable bits: PSE, PAE, PGE, OSFXSR, etc.
 */
static inline uint64_t arch_cr4_read(void) {
    uint64_t cr4;
    asm volatile ("mov %%cr4, %0" : "=r"(cr4));
    return cr4;
}

/**
 * arch_cr4_write - Write CR4 register
 * @value: Value to write to CR4
 *
 * Enables/disables CPU features controlled by CR4.
 */
static inline void arch_cr4_write(uint64_t value) {
    asm volatile ("mov %0, %%cr4" : : "r"(value) : "memory");
}

#endif /* EMERGENCE_ARCH_X86_64_CR_H */
