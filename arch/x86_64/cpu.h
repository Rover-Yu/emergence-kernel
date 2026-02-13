/* Emergence Kernel - x86-64 CPU Instruction Wrappers */

#ifndef EMERGENCE_ARCH_X86_64_CPU_H
#define EMERGENCE_ARCH_X86_64_CPU_H

#include <stdint.h>

/**
 * arch_halt - Halt the CPU until next interrupt
 *
 * Executes HLT instruction. CPU enters low-power state
 * until interrupt, NMI, or reset occurs.
 */
static inline void arch_halt(void) {
    asm volatile ("hlt");
}

/**
 * arch_nop - No-operation instruction
 *
 * Executes NOP. Useful for timing delays or alignment.
 */
static inline void arch_nop(void) {
    asm volatile ("nop");
}

/**
 * arch_cpuid - Execute CPUID instruction
 * @leaf: CPUID leaf (EAX value)
 * @eax: Output EAX register value
 * @ebx: Output EBX register value
 * @ecx: Output ECX register value
 * @edx: Output EDX register value
 *
 * Queries CPU information and features. Different leaves return
 * vendor info, feature flags, cache info, topology, etc.
 */
static inline void arch_cpuid(uint32_t leaf, uint32_t *eax,
                           uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
    uint32_t a, b, c, d;
    asm volatile ("cpuid"
                  : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                  : "a"(leaf));
    if (eax) *eax = a;
    if (ebx) *ebx = b;
    if (ecx) *ecx = c;
    if (edx) *edx = d;
}

#endif /* EMERGENCE_ARCH_X86_64_CPU_H */
