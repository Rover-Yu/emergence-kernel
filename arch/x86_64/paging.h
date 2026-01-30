#ifndef ARCH_X86_64_PAGING_H
#define ARCH_X86_64_PAGING_H

#include <stdint.h>

/* Page Table Entry (PTE) flags for x86_64 - from Nested Kernel paper */
#define X86_PTE_PRESENT    (1ULL << 0)   /* Bit 0: Present */
#define X86_PTE_WRITABLE   (1ULL << 1)   /* Bit 1: Read/Write (0 = read-only for protection) */
#define X86_PTE_USER       (1ULL << 2)   /* Bit 2: User/Supervisor */
#define X86_PTE_PWT        (1ULL << 3)   /* Bit 3: Page Write Through */
#define X86_PTE_PCD        (1ULL << 4)   /* Bit 4: Page Cache Disable */
#define X86_PTE_ACCESSED   (1ULL << 5)   /* Bit 5: Accessed */
#define X86_PTE_DIRTY      (1ULL << 6)   /* Bit 6: Dirty */
#define X86_PTE_PS         (1ULL << 7)   /* Bit 7: Page Size (1 = 2MB/1GB) */
#define X86_PTE_GLOBAL     (1ULL << 8)   /* Bit 8: Global */

/* Standard 2MB page flags (Present + Writable + PS) = 0x183 */
#define X86_PD_FLAGS_2MB   (X86_PTE_PRESENT | X86_PTE_WRITABLE | X86_PTE_PS)

/* Read-only 2MB page flags (Present + PS, no Writable) = 0x181
 * This implements Invariant 1: protected data is read-only while outer kernel executes */
#define X86_PD_FLAGS_2MB_RO (X86_PTE_PRESENT | X86_PTE_PS)

#endif /* ARCH_X86_64_PAGING_H */
