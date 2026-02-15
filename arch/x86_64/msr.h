/* Emergence Kernel - x86-64 Model-Specific Register (MSR) Access */

#ifndef EMERGENCE_ARCH_X86_64_MSR_H
#define EMERGENCE_ARCH_X86_64_MSR_H

#include <stdint.h>

/**
 * arch_msr_read - Read Model-Specific Register
 * @msr: MSR address
 *
 * Returns: 64-bit MSR value
 *
 * Uses RDMSR instruction. MSR is specified in ECX, result in EDX:EAX.
 */
static inline uint64_t arch_msr_read(uint32_t msr) {
    uint32_t low, high;
    asm volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr) : "memory");
    return ((uint64_t)high << 32) | low;
}

/**
 * arch_msr_write - Write Model-Specific Register
 * @msr: MSR address
 * @value: 64-bit value to write
 *
 * Uses WRMSR instruction. MSR in ECX, value in EDX:EAX.
 */
static inline void arch_msr_write(uint32_t msr, uint64_t value) {
    uint32_t low = value & 0xFFFFFFFF;
    uint32_t high = value >> 32;
    asm volatile ("wrmsr" : : "c"(msr), "a"(low), "d"(high) : "memory");
}

#endif /* EMERGENCE_ARCH_X86_64_MSR_H */
