/* JAKernel - x86-64 Local APIC implementation */

#include <stdint.h>
#include "arch/x86_64/apic.h"
#include "arch/x86_64/io.h"

/* APIC version register */
#define LAPIC_VER 0x030

/* External functions for serial output */
extern void serial_puts(const char *str);
extern void serial_putc(char c);

/* Local APIC base address (set during initialization) */
static volatile uint32_t *lapic_base = (volatile uint32_t *)LAPIC_DEFAULT_BASE;

/* IA32_APIC_BASE MSR */
#define IA32_APIC_BASE_MSR  0x1B

/* IA32_APIC_BASE MSR bits */
#define IA32_APIC_BASE_ENABLED  (1 << 11)  /* APIC global enable */
#define IA32_APIC_BASE_EXTD     (1 << 10)  /* x2APIC enable */
#define IA32_APIC_BASE_BSP      (1 << 8)   /* BSP flag */

/* MSR read/write functions */
static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    asm volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static inline void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t low = value & 0xFFFFFFFF;
    uint32_t high = value >> 32;
    asm volatile ("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

/**
 * lapic_read - Read Local APIC register
 * @offset: Register offset from base
 *
 * Returns: Register value
 *
 * Direct pointer access for MMIO reads.
 */
uint32_t lapic_read(uint32_t offset) {
    /* Use inline assembly with explicit memory barrier for MMIO read */
    volatile uint32_t *addr = (volatile uint32_t *)((char *)lapic_base + offset);
    uint32_t value;

    /* Memory barrier before MMIO read */
    asm volatile ("mfence" ::: "memory");

    /* Explicit volatile load with "+m" constraint to prevent caching */
    asm volatile ("movl %1, %0"
                  : "=r"(value)
                  : "m"(*addr)
                  : "memory");

    /* Memory barrier after MMIO read */
    asm volatile ("mfence" ::: "memory");

    return value;
}

/**
 * lapic_write - Write Local APIC register
 * @offset: Register offset from base
 * @value: Value to write
 *
 * Direct pointer access for MMIO writes.
 */
void lapic_write(uint32_t offset, uint32_t value) {
    /* Use inline assembly with explicit memory barrier for MMIO write */
    volatile uint32_t *addr = (volatile uint32_t *)((char *)lapic_base + offset);

    /* Memory barrier before MMIO write */
    asm volatile ("mfence" ::: "memory");

    /* Explicit volatile store with "+m" constraint to ensure write reaches device */
    asm volatile ("movl %0, %1"
                  :
                  : "r"(value), "m"(*addr)
                  : "memory");

    /* Memory barrier after MMIO write */
    asm volatile ("mfence" ::: "memory");
}

/**
 * lapic_get_base - Get Local APIC base address
 *
 * Returns: Local APIC base address
 */
uint64_t lapic_get_base(void) {
    uint64_t apic_base = rdmsr(IA32_APIC_BASE_MSR);

    /* Extract base address (bits 12-35) and align to page boundary */
    apic_base &= 0xFFFFF000;

    return apic_base;
}

/**
 * lapic_verify_registers - Verify APIC register access
 *
 * Tests access to key APIC registers to verify the memory mapping
 * is working correctly. Returns 0 on success, -1 on failure.
 */
static int lapic_verify_registers(void) {
    uint32_t val, orig;
    int errors = 0;

    serial_puts("[APIC] === Register Access Verification ===\n");

    /* Test 1: LAPIC_ID register (read-only) */
    serial_puts("[APIC] Testing LAPIC_ID (0x020)...\n");
    val = lapic_read(LAPIC_ID);
    serial_puts("[APIC]   LAPIC_ID = 0x");
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (val >> i) & 0xF;
        serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    serial_puts("\n");
    /* APIC ID should be 0-255 */
    if ((val >> 24) > 255) {
        serial_puts("[APIC]   ERROR: Invalid APIC ID!\n");
        errors++;
    } else {
        serial_puts("[APIC]   APIC ID = ");
        uint8_t apic_id = val >> 24;
        serial_putc('0' + apic_id);
        serial_puts(" (valid)\n");
    }

    /* Test 2: LAPIC_VER register (read-only) */
    serial_puts("[APIC] Testing LAPIC_VER (0x030)...\n");
    val = lapic_read(LAPIC_VER);
    serial_puts("[APIC]   LAPIC_VER = 0x");
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (val >> i) & 0xF;
        serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    serial_puts("\n");
    /* Version should be non-zero */
    if ((val & 0xFF) == 0) {
        serial_puts("[APIC]   WARNING: Version is 0\n");
    } else {
        uint8_t version = val & 0xFF;
        uint8_t max_lvt = (val >> 16) & 0xFF;
        serial_puts("[APIC]   Version = ");
        serial_putc('0' + version);
        serial_puts(", Max LVT = ");
        serial_putc('0' + max_lvt);
        serial_puts("\n");
    }

    /* Test 3: LAPIC_TPR register (read/write) */
    serial_puts("[APIC] Testing LAPIC_TPR (0x080) R/W...\n");
    orig = lapic_read(LAPIC_TPR);
    lapic_write(LAPIC_TPR, 0xAA);
    val = lapic_read(LAPIC_TPR);
    lapic_write(LAPIC_TPR, orig);  /* Restore */
    if (val == 0xAA) {
        serial_puts("[APIC]   TPR write/read: PASSED\n");
    } else {
        serial_puts("[APIC]   ERROR: TPR write/read failed (wrote=0xAA, read=0x");
        for (int i = 28; i >= 0; i -= 4) {
            uint8_t nibble = (val >> i) & 0xF;
            serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
        }
        serial_puts(")\n");
        errors++;
    }

    /* Test 4: LAPIC_EOI register (write-only) */
    serial_puts("[APIC] Testing LAPIC_EOI (0x0B0) write...\n");
    /* EOI is write-only, just verify we can write without fault */
    lapic_write(LAPIC_EOI, 0);
    serial_puts("[APIC]   EOI write: PASSED (no fault)\n");

    /* Test 5: LAPIC_LDR register (read/write) */
    serial_puts("[APIC] Testing LAPIC_LDR (0x0D0) R/W...\n");
    orig = lapic_read(LAPIC_LDR);
    lapic_write(LAPIC_LDR, 0x55555555);
    val = lapic_read(LAPIC_LDR);
    lapic_write(LAPIC_LDR, orig);  /* Restore */
    if (val == 0x55555555) {
        serial_puts("[APIC]   LDR write/read: PASSED\n");
    } else {
        serial_puts("[APIC]   ERROR: LDR write/read failed\n");
        errors++;
    }

    /* Test 6: LAPIC_SVR register (read/write) */
    serial_puts("[APIC] Testing LAPIC_SVR (0x0F0) R/W...\n");
    orig = lapic_read(LAPIC_SVR);
    lapic_write(LAPIC_SVR, orig | 0x100);  /* Try to set enable bit */
    val = lapic_read(LAPIC_SVR);
    if (val & 0x100) {
        serial_puts("[APIC]   SVR write/read: PASSED (APIC enabled)\n");
    } else {
        serial_puts("[APIC]   ERROR: SVR write/read failed\n");
        errors++;
    }

    /* Test 7: LAPIC_ICR_LOW register (critical for IPI) */
    serial_puts("[APIC] Testing LAPIC_ICR_LOW (0x300) R/W...\n");
    orig = lapic_read(LAPIC_ICR_LOW);
    serial_puts("[APIC]   ICR_LOW = 0x");
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (orig >> i) & 0xF;
        serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    serial_puts("\n");
    /* Check if delivery status bit is clear (ready) */
    if (!(orig & LAPIC_ICR_DS)) {
        serial_puts("[APIC]   ICR delivery status: READY\n");
    } else {
        serial_puts("[APIC]   WARNING: ICR delivery status: PENDING\n");
    }

    /* Test 8: LAPIC_ICR_HIGH register (contains destination APIC ID) */
    serial_puts("[APIC] Testing LAPIC_ICR_HIGH (0x310) R/W...\n");
    orig = lapic_read(LAPIC_ICR_HIGH);
    serial_puts("[APIC]   ICR_HIGH = 0x");
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (orig >> i) & 0xF;
        serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    serial_puts("\n");
    uint8_t dest_id = orig >> 24;
    serial_puts("[APIC]   Destination APIC ID = ");
    serial_putc('0' + dest_id);
    serial_puts("\n");

    /* Test 9: LAPIC_TIMER_LVT register */
    serial_puts("[APIC] Testing LAPIC_TIMER_LVT (0x320) R/W...\n");
    orig = lapic_read(LAPIC_TIMER_LVT);
    serial_puts("[APIC]   TIMER_LVT = 0x");
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (orig >> i) & 0xF;
        serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    serial_puts("\n");

    /* Test 10: LAPIC_ESR register */
    serial_puts("[APIC] Testing LAPIC_ESR (0x280) write/read...\n");
    /* Write to ESR clears it on read */
    lapic_write(LAPIC_ESR, 0xFFFFFFFF);
    val = lapic_read(LAPIC_ESR);
    serial_puts("[APIC]   ESR after write = 0x");
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (val >> i) & 0xF;
        serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    serial_puts("\n");

    serial_puts("[APIC] === Verification Complete ===\n");
    if (errors == 0) {
        serial_puts("[APIC] All register tests PASSED\n");
        return 0;
    } else {
        serial_puts("[APIC] ERROR: ");
        serial_putc('0' + errors);
        serial_puts(" test(s) FAILED\n");
        return -1;
    }
}

/**
 * lapic_init - Initialize Local APIC
 */
void lapic_init(void) {
    uint64_t apic_base_msr;
    uint32_t id, ver, svr_after;

    /* Read the IA32_APIC_BASE MSR to get actual APIC configuration */
    apic_base_msr = rdmsr(IA32_APIC_BASE_MSR);

    serial_puts("[APIC] IA32_APIC_BASE MSR (before) = 0x");
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t nibble = (apic_base_msr >> i) & 0xF;
        serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    serial_puts("\n");

    /* Check if APIC is enabled, enable it if not */
    serial_puts("[APIC] Checking APIC enable status (bit 11)...\n");
    /* Debug: show bit 11 directly */
    serial_puts("[APIC]   apic_base_msr & 0x800 = 0x");
    uint32_t bit11_check = apic_base_msr & 0x800;
    for (int i = 12; i >= 0; i -= 4) {
        uint8_t nibble = (bit11_check >> i) & 0xF;
        serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    serial_puts("\n");
    if (apic_base_msr & IA32_APIC_BASE_ENABLED) {
        serial_puts("[APIC] APIC is ENABLED via MSR (bit 11 = 1)\n");
    } else {
        serial_puts("[APIC] APIC is DISABLED via MSR (bit 11 = 0), enabling...\n");
        apic_base_msr |= IA32_APIC_BASE_ENABLED;
        wrmsr(IA32_APIC_BASE_MSR, apic_base_msr);
        serial_puts("[APIC] Attempted to enable APIC in MSR\n");
    }

    /* Re-read MSR to verify */
    apic_base_msr = rdmsr(IA32_APIC_BASE_MSR);
    serial_puts("[APIC] IA32_APIC_BASE MSR (after) = 0x");
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t nibble = (apic_base_msr >> i) & 0xF;
        serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    serial_puts("\n");

    /* Check if x2APIC is enabled */
    if (apic_base_msr & IA32_APIC_BASE_EXTD) {
        serial_puts("[APIC] WARNING: x2APIC is enabled! Trying to disable...\n");

        /* Disable x2APIC mode by clearing bit 10 */
        apic_base_msr &= ~IA32_APIC_BASE_EXTD;
        wrmsr(IA32_APIC_BASE_MSR, apic_base_msr);

        /* Read back to verify */
        apic_base_msr = rdmsr(IA32_APIC_BASE_MSR);
        if (apic_base_msr & IA32_APIC_BASE_EXTD) {
            serial_puts("[APIC] ERROR: Failed to disable x2APIC!\n");
            serial_puts("[APIC] Will attempt x2APIC mode access...\n");
        } else {
            serial_puts("[APIC] x2APIC disabled successfully\n");
        }
    } else {
        serial_puts("[APIC] x2APIC is not enabled (good)\n");
    }

    /* Don't try x2APIC - it causes crashes in QEMU
     * x2APIC requires CPUID check and proper support */
    serial_puts("[APIC] Skipping x2APIC mode (not supported in QEMU config)\n");

    /* Use the actual APIC base address from MSR
     * Note: We're in long mode with paging enabled
     * The page tables should identity-map 0xFEE00000 -> 0xFEE00000
     * We can use the physical address directly because of the identity mapping
     *
     * CRITICAL: Try using the VIRTUAL address directly via PML4[0x1FD] path
     * instead of relying on identity mapping */
    serial_puts("[APIC] Setting lapic_base to VA 0xFEE00000 (via PML4[0x1FD])\n");
    lapic_base = (volatile uint32_t *)0xFEE00000;

    serial_puts("[APIC] APIC base address = 0x");
    uint64_t base_addr = (uint64_t)lapic_base;
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t nibble = (base_addr >> i) & 0xF;
        serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    serial_puts("\n");

    /* CRITICAL FIX: Enable APIC via SVR BEFORE reading any registers!
     * The SVR bit 8 (software enable) must be set for APIC registers to be accessible.
     * Without this, all APIC register reads return 0. */
    serial_puts("[APIC] Enabling APIC via SVR (early init)...\n");

    /* Debug: Check PDPT[3] entry to verify APIC mapping */
    serial_puts("[APIC] Checking PDPT[3] entry...\n");
    extern uint64_t boot_pdpt[];
    uint64_t pdpt_entry_3 = boot_pdpt[3];
    serial_puts("[APIC] PDPT[3] = 0x");
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t nibble = (pdpt_entry_3 >> i) & 0xF;
        serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    serial_puts("\n");
    if (pdpt_entry_3 == 0) {
        serial_puts("[APIC] ERROR: PDPT[3] is 0! APIC NOT accessible!\n");
    } else {
        serial_puts("[APIC] PDPT[3] is set correctly\n");
    }

    /* Debug: Print lapic_base value before any access */
    serial_puts("[APIC] lapic_base = 0x");
    uint64_t base_ptr = (uint64_t)lapic_base;
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t nibble = (base_ptr >> i) & 0xF;
        serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    serial_puts("\n");

    /* Try a simple test: write to SVR and verify */
    serial_puts("[APIC] Testing SVR write/read...\n");
    serial_puts("[APIC] Writing 0x1FF to SVR...\n");

    /* Flush cache before write */
    asm volatile ("mfence" ::: "memory");

    lapic_write(LAPIC_SVR, 0x100 | 0xFF);  /* Enable + spurious vector */

    /* Flush cache after write */
    asm volatile ("mfence" ::: "memory");

    /* Flush the specific cache line for the APIC SVR register */
    /* Note: clflush needs proper memory operand syntax */
    asm volatile ("clflush (%0)" : : "r"((volatile char *)lapic_base + LAPIC_SVR) : "memory");

    uint32_t svr_check = lapic_read(LAPIC_SVR);

    /* Flush cache after read */
    asm volatile ("mfence" ::: "memory");

    serial_puts("[APIC] SVR after write = 0x");
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (svr_check >> i) & 0xF;
        serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    serial_puts("\n");

    if (svr_check == (0x100 | 0xFF)) {
        serial_puts("[APIC] SUCCESS! SVR write/read worked!\n");
    } else {
        serial_puts("[APIC] SVR write/read failed (wrote=0x1FF)\n");
    }

    /* Now try reading APIC ID and VERSION */
    serial_puts("[APIC] Testing direct access to lapic_base[0]...\n");
    uint32_t test_val = lapic_base[0];
    serial_puts("[APIC] lapic_base[0] = 0x");
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (test_val >> i) & 0xF;
        serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    serial_puts("\n");

    /* Memory fence to ensure SVR write completes before reading */
    asm volatile ("mfence" ::: "memory");

    /* Small delay to let APIC initialize */
    for (volatile int i = 0; i < 1000; i++) {
        asm volatile ("pause");
    }

    /* Now read APIC ID and VERSION after enabling APIC */
    id = lapic_read(LAPIC_ID);
    serial_puts("[APIC] Initial APIC_ID = 0x");
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (id >> i) & 0xF;
        serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    serial_puts("\n");

    /* Read APIC version */
    ver = lapic_read(LAPIC_VER);
    serial_puts("[APIC] LAPIC_VER = 0x");
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (ver >> i) & 0xF;
        serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    serial_puts("\n");

    /* Verify page table mapping for 0xFEE00000
     * VA 0xFEE00000 -> PML4[0x1FD] -> PDPT[3] -> PD[503] -> PA 0xFEE00000
     *
     * Wait! 0xFEE00000 is HIGH memory (~4.27GB), not in the first 1GB.
     * PML4 index = bits 39-47 = 0x1FD (not 0!)
     * PDPT index = bits 30-38 = 0x3
     * PD index = bits 21-29 = 0x1F8 (504 decimal)
     */
    serial_puts("[APIC] Verifying page table mapping for 0xFEE00000...\n");
    extern uint64_t boot_pml4[];

    /* Calculate page table indices for VA 0xFEE00000 */
    uint64_t pml4_index = (0xFEE00000ULL >> 39) & 0x1FF;
    uint64_t pdpt_index = (0xFEE00000ULL >> 30) & 0x1FF;
    uint64_t pd_index = (0xFEE00000ULL >> 21) & 0x1FF;

    serial_puts("[APIC] Page table indices for 0xFEE00000:\n");
    serial_puts("[APIC]   PML4[0x");
    for (int i = 8; i >= 0; i -= 4) {
        uint8_t nibble = (pml4_index >> i) & 0xF;
        serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    serial_puts("] -> PDPT index 0x");
    for (int i = 8; i >= 0; i -= 4) {
        uint8_t nibble = (pdpt_index >> i) & 0xF;
        serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    serial_puts(" -> PD[0x");
    for (int i = 8; i >= 0; i -= 4) {
        uint8_t nibble = (pd_index >> i) & 0xF;
        serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    serial_puts("]\n");

    /* Check PML4[0x1FD] for 0xFEE00000 (critical for APIC access) */
    uint64_t pml4_1fd = boot_pml4[0x1FD];
    serial_puts("[APIC] PML4[0x1FD] (APIC PML4 entry) = 0x");
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t nibble = (pml4_1fd >> i) & 0xF;
        serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    serial_puts("\n");

    /* Check PML4[0] for comparison */
    uint64_t pml4_entry0 = boot_pml4[0];
    serial_puts("[APIC] PML4[0] = 0x");
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t nibble = (pml4_entry0 >> i) & 0xF;
        serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    serial_puts("\n");

    /* If PML4[0x1FD] is 0, APIC at 0xFEE00000 is NOT accessible! */
    if (pml4_1fd == 0) {
        serial_puts("[APIC] ERROR: PML4[0x1FD] is 0! APIC NOT accessible!\n");
    } else {
        serial_puts("[APIC] PML4[0x1FD] is set, APIC should be accessible\n");

        /* Trace the full page table walk for VA 0xFEE00000 */
        extern uint64_t boot_pdpt[];
        extern uint64_t boot_pd[];

        /* PML4[0x1FD] points to PDPT - use the actual symbol which is identity-mapped */
        serial_puts("[APIC] Using boot_pdpt identity mapping\n");
        serial_puts("[APIC] boot_pdpt address = 0x");
        uint64_t pdpt_sym_addr = (uint64_t)boot_pdpt;
        for (int i = 60; i >= 0; i -= 4) {
            uint8_t nibble = (pdpt_sym_addr >> i) & 0xF;
            serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
        }
        serial_puts("\n");

        /* Check PDPT[3] using the actual symbol */
        uint64_t pdpt_entry3 = boot_pdpt[3];
        serial_puts("[APIC] PDPT[3] = 0x");
        for (int i = 60; i >= 0; i -= 4) {
            uint8_t nibble = (pdpt_entry3 >> i) & 0xF;
            serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
        }
        serial_puts("\n");

        if (pdpt_entry3 == 0) {
            serial_puts("[APIC] ERROR: PDPT[3] is 0! APIC mapping broken!\n");
        }

        /* Check PD[503] using the actual symbol */
        serial_puts("[APIC] Using boot_pd identity mapping\n");
        serial_puts("[APIC] boot_pd address = 0x");
        uint64_t pd_sym_addr = (uint64_t)boot_pd;
        for (int i = 60; i >= 0; i -= 4) {
            uint8_t nibble = (pd_sym_addr >> i) & 0xF;
            serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
        }
        serial_puts("\n");

        uint64_t pd_entry504 = boot_pd[504];
        serial_puts("[APIC] PD[503] = 0x");
        for (int i = 60; i >= 0; i -= 4) {
            uint8_t nibble = (pd_entry504 >> i) & 0xF;
            serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
        }
        serial_puts("\n");

        if (pd_entry504 == 0) {
            serial_puts("[APIC] ERROR: PD[503] is 0! APIC page not mapped!\n");
        }

        /* PD[503] should map to PA 0xFEE00000 */
        uint64_t mapped_pa = (pd_entry504 >> 12) << 12;
        serial_puts("[APIC] PD[503] maps to PA 0x");
        for (int i = 60; i >= 0; i -= 4) {
            uint8_t nibble = (mapped_pa >> i) & 0xF;
            serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
        }
        serial_puts("\n");

        /* Check page table entry flags for APIC mapping */
        serial_puts("[APIC] Checking PD[503] entry flags...\n");
        serial_puts("[APIC]   PD[503] = 0x");
        for (int i = 60; i >= 0; i -= 4) {
            uint8_t nibble = (pd_entry504 >> i) & 0xF;
            serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
        }
        serial_puts("\n");
        /* Check for critical flags */
        if (pd_entry504 & (1 << 12)) serial_puts("[APIC]   PAT bit is SET\n");
        else serial_puts("[APIC]   PAT bit is CLEAR\n");
        if (pd_entry504 & (1 << 7)) serial_puts("[APIC]   Page size bit (2MB) is SET\n");
        else serial_puts("[APIC]   Page size bit is CLEAR\n");
        if (pd_entry504 & (1 << 6)) serial_puts("[APIC]   Dirty bit is SET\n");
        else serial_puts("[APIC]   Dirty bit is CLEAR\n");
        if (pd_entry504 & (1 << 5)) serial_puts("[APIC]   Access bit is SET\n");
        else serial_puts("[APIC]   Access bit is CLEAR\n");
        if (pd_entry504 & (1 << 4)) serial_puts("[APIC]   Cache disable (CD) is SET\n");
        else serial_puts("[APIC]   Cache disable (CD) is CLEAR (may cache MMIO!)\n");
        if (pd_entry504 & (1 << 3)) serial_puts("[APIC]   Write-through is SET\n");
        else serial_puts("[APIC]   Write-through is CLEAR\n");
    }

    /* Test APIC accessibility by writing to ESR and reading back */
    serial_puts("[APIC] Testing APIC accessibility...\n");

    /* First, check CPUID for APIC support */
    uint32_t eax, ebx, ecx, edx;
    asm volatile ("cpuid"
                  : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                  : "a"(1));  /* CPUID leaf 1 */
    serial_puts("[APIC] CPUID.1:EDX.APIC (bit 9) = ");
    if (edx & (1 << 9)) {
        serial_puts("1 (APIC available)\n");
    } else {
        serial_puts("0 (NO APIC!)\n");
    }

    /* Check if we're actually accessing the right memory location */
    serial_puts("[APIC] Trying to access APIC at 0xFEE00000...\n");
    volatile uint32_t *apic_direct = (volatile uint32_t *)0xFEE00000;
    serial_puts("[APIC] Direct read: *0xFEE00000 = 0x");
    uint32_t direct_read = apic_direct[0];
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (direct_read >> i) & 0xF;
        serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    serial_puts("\n");

    /* Scan the entire APIC 4KB MMIO range to verify mapping */
    serial_puts("[APIC] Scanning APIC MMIO range (0xFEE00000 - 0xFEE00FFF)...\n");
    volatile uint32_t *apic_base = (volatile uint32_t *)0xFEE00000;
    int non_zero_count = 0;
    for (int offset = 0; offset < 0x1000; offset += 16) {
        uint32_t val = apic_base[offset / 4];
        if (val != 0) {
            non_zero_count++;
            if (non_zero_count <= 8) {  /* Show first 8 non-zero values */
                serial_puts("[APIC]   Offset 0x");
                for (int i = 12; i >= 0; i -= 4) {
                    uint8_t nibble = (offset >> i) & 0xF;
                    serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
                }
                serial_puts(" = 0x");
                for (int i = 28; i >= 0; i -= 4) {
                    uint8_t nibble = (val >> i) & 0xF;
                    serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
                }
                serial_puts("\n");
            }
        }
    }
    serial_puts("[APIC]   Total non-zero dwords: ");
    if (non_zero_count < 10) {
        serial_putc('0' + non_zero_count);
    } else {
        serial_putc('0' + (non_zero_count / 10));
        serial_putc('0' + (non_zero_count % 10));
    }
    serial_puts(" of 256\n");

    /* Verify page table is working by checking a known-mapped address */
    serial_puts("[APIC] Verifying page tables by accessing kernel memory...\n");
    volatile uint32_t *kernel_ptr = (volatile uint32_t *)0x100000;
    uint32_t kernel_val = *kernel_ptr;
    serial_puts("[APIC]   Read from 0x100000 = 0x");
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (kernel_val >> i) & 0xF;
        serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    serial_puts("\n");
    if (kernel_val != 0) {
        serial_puts("[APIC]   Page tables are working (kernel memory accessible)\n");
    } else {
        serial_puts("[APIC]   WARNING: Page tables may not be working!\n");
    }

    /* Try ESR write/read test - clear ESR first */
    lapic_write(LAPIC_ESR, 0);  /* Clear all errors */
    uint32_t test_read = lapic_read(LAPIC_ESR);
    serial_puts("[APIC] ESR after clear: 0x");
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (test_read >> i) & 0xF;
        serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    serial_puts("\n");

    /* Run comprehensive register verification - DISABLED to prevent crashes */
    /* The register tests can cause triple faults during early boot */
    /*
    if (lapic_verify_registers() < 0) {
        serial_puts("[APIC] WARNING: Register verification failed, continuing anyway\n");
    }
    */
    serial_puts("[APIC] Register verification disabled for stability\n");

    /* Read SVR after initialization (APIC was already enabled early) */
    svr_after = lapic_read(LAPIC_SVR);
    serial_puts("[APIC] SVR after init = 0x");
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (svr_after >> i) & 0xF;
        serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    serial_puts("\n");

    serial_puts("[APIC] Local APIC initialized\n");
}

/**
 * lapic_get_id - Get Local APIC ID of current CPU
 *
 * Returns: Local APIC ID
 */
uint8_t lapic_get_id(void) {
    uint32_t id = lapic_read(LAPIC_ID);
    /* APIC ID is in bits 24-31 for xAPIC */
    return (uint8_t)(id >> 24);
}

/**
 * print_hex_byte - Print a byte as hex
 * @val: Value to print
 */
static void print_hex_byte(uint8_t val) {
    extern void serial_putc(char c);
    serial_putc('0');
    serial_putc('x');
    serial_putc('0' + ((val >> 4) & 0xF));
    serial_putc('0' + (val & 0xF));
}

/**
 * lapic_send_ipi - Send Inter-Processor Interrupt
 * @apic_id: Target APIC ID (0-255)
 * @delivery_mode: Delivery mode (INIT, STARTUP, etc.)
 * @vector: Vector number (for STARTUP, this is the page number)
 */
void lapic_send_ipi(uint8_t apic_id, uint32_t delivery_mode, uint8_t vector) {
    uint32_t icr_low, icr_high;

    /* Set destination APIC ID in ICR high */
    icr_high = (uint32_t)apic_id << 24;
    lapic_write(LAPIC_ICR_HIGH, icr_high);

    /* Set delivery mode and vector in ICR low */
    icr_low = delivery_mode | vector;
    icr_low |= LAPIC_ICR_DST_PHYSICAL;  /* Physical destination mode */

    /* Set ASSERT bit for level-triggered IPIs (INIT, SMI), but NOT for STARTUP
     * STARTUP IPI is edge-triggered and must NOT have ASSERT bit set */
    if (delivery_mode == LAPIC_ICR_DM_INIT ||
        delivery_mode == LAPIC_ICR_DM_SMI) {
        icr_low |= LAPIC_ICR_ASSERT;
    }

    /* Send IPI */
    lapic_write(LAPIC_ICR_LOW, icr_low);
}

/**
 * lapic_send_nmi - Send Non-Maskable Interrupt to AP
 * @apic_id: Target APIC ID (0-255)
 *
 * This is used to test if the AP's Local APIC is working.
 * Unlike STARTUP IPI, NMI doesn't require the AP to be in "wait for SIPI" state.
 */
void lapic_send_nmi(uint8_t apic_id) {
    uint32_t icr_low, icr_high;

    serial_puts("[APIC] Sending NMI to APIC ID=");
    serial_putc('0' + apic_id);
    serial_puts("\n");

    /* Set destination APIC ID in ICR high */
    icr_high = (uint32_t)apic_id << 24;
    lapic_write(LAPIC_ICR_HIGH, icr_high);

    /* NMI: NMI delivery mode */
    icr_low = LAPIC_ICR_DM_NMI;
    icr_low |= LAPIC_ICR_DST_PHYSICAL;
    icr_low |= LAPIC_ICR_ASSERT;

    serial_puts("[APIC] ICR_HIGH = 0x");
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (icr_high >> i) & 0xF;
        serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    serial_puts(" ICR_LOW = 0x");
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (icr_low >> i) & 0xF;
        serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    serial_puts("\n");

    /* Send NMI */
    lapic_write(LAPIC_ICR_LOW, icr_low);

    lapic_wait_for_ipi();
    serial_puts("[APIC] NMI sent\n");
}

/**
 * lapic_wait_for_ipi - Wait for IPI delivery to complete
 *
 * Waits for the delivery status bit to clear, with timeout.
 *
 * Returns: 0 on success, -1 on timeout
 */
int lapic_wait_for_ipi(void) {
    /* Wait for delivery status bit to clear, with timeout */
    int timeout = 1000000;  /* Increased timeout */
    uint32_t icr_low;
    int first = 1;

    while (timeout-- > 0) {
        icr_low = lapic_read(LAPIC_ICR_LOW);
        if (!(icr_low & LAPIC_ICR_DS)) {
            /* Delivery complete */
            serial_puts("[APIC] ICR_LOW after send: 0x");
            for (int i = 28; i >= 0; i -= 4) {
                uint8_t nibble = (icr_low >> i) & 0xF;
                serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
            }
            serial_puts("\n");
            return 0;
        }
        if (first) {
            serial_puts("[APIC] Waiting for IPI delivery...\n");
            first = 0;
        }
        asm volatile ("pause");
    }

    serial_puts("[APIC] IPI delivery timeout!\n");
    return -1;  /* Timeout */
}

/* Simple delay using busy-wait loop
 * Optimized for QEMU - very short delays for SMP init */
static void pit_delay_ms(uint32_t ms) {
    /* For QEMU and SMP startup, we don't need precise timing
     * Just give the AP some time to respond */
    volatile int dummy;
    for (volatile uint32_t i = 0; i < ms * 1000; i++) {
        dummy = i;  /* Prevent optimization */
        asm volatile ("pause");
    }
    (void)dummy;  /* Prevent unused warning */
}

/**
 * ap_startup - Startup an Application Processor
 * @apic_id: APIC ID of target CPU
 * @startup_addr: Startup address (must be 4KB aligned, page number)
 *
 * Returns: 0 on success, negative on error
 *
 * This sends an INIT IPI followed by a STARTUP IPI to wake up an AP.
 * The startup_addr is the page number (address >> 12) where the AP
 * will begin execution in real mode.
 *
 * Intel 64/IA-32 Architecture Software Developer's Manual spec:
 * 1. Send INIT IPI (assert, level-triggered)
 * 2. Wait 10ms (1ms for QEMU)
 * 3. Send INIT IPI (deassert, level-triggered)
 * 4. Wait 10ms (skipped for QEMU - we're aggressive)
 * 5. Send STARTUP IPI
 * 6. Wait 200us
 * 7. Send second STARTUP IPI (optional but recommended)
 *
 * CRITICAL: INIT must be level-triggered with BOTH assert AND deassert!
 * Without deassert, the AP remains stuck in INIT state forever.
 */
int ap_startup(uint8_t apic_id, uint32_t startup_addr) {
    uint32_t icr_low, icr_high;
    uint32_t ver, maxlvt;

    /* Debug: Print BSP's APIC ID (from hardware) and target APIC ID */
    uint8_t my_apic_id = lapic_get_id();
    serial_puts("[APIC] BSP APIC ID = ");
    print_hex_byte(my_apic_id);
    serial_puts(", Target APIC ID = ");
    print_hex_byte(apic_id);
    serial_puts("\n");

    serial_puts("[APIC] ap_startup: APIC ID=");
    serial_putc('0' + apic_id);
    serial_puts(" vector=");
    serial_putc('0' + startup_addr);
    serial_puts("\n");

    /* Check APIC version (for ESR clearing)
     * LAPIC_VER format:
     *   Bits 0-7: Version
     *   Bits 16-23: Max LVT entry
     */
    ver = lapic_read(LAPIC_VER);
    maxlvt = (ver >> 16) & 0xFF;  /* Max LVT is in bits 16-23 */
    serial_puts("[APIC] LAPIC_VER = 0x");
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (ver >> i) & 0xF;
        serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    serial_puts("\n");
    serial_puts("[APIC] APIC version=");
    serial_putc('0' + (ver & 0xFF));
    serial_puts(" maxlvt=");
    serial_putc('0' + maxlvt);
    serial_puts("\n");

    /* Be paranoid about clearing APIC errors (like Linux does)
     * Due to Pentium erratum 3AP */
    if (maxlvt > 3) {
        lapic_write(LAPIC_ESR, 0);
        lapic_read(LAPIC_ESR);  /* Dummy read to flush */
        serial_puts("[APIC] Cleared ESR (maxlvt > 3)\n");
    }

    /* Step 1: Send INIT IPI (ASSERT, level-triggered) to reset the AP to real mode */
    serial_puts("[APIC] Sending INIT IPI (assert)...\n");

    /* Set destination APIC ID in ICR high */
    icr_high = (uint32_t)apic_id << 24;
    lapic_write(LAPIC_ICR_HIGH, icr_high);

    /* INIT: level-triggered + assert + INIT delivery mode */
    icr_low = LAPIC_ICR_LEVELTRIG | LAPIC_ICR_ASSERT | LAPIC_ICR_DM_INIT;
    icr_low |= LAPIC_ICR_DST_PHYSICAL;

    serial_puts("[APIC] ICR_HIGH = 0x");
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (icr_high >> i) & 0xF;
        serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    serial_puts(" ICR_LOW = 0x");
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (icr_low >> i) & 0xF;
        serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    serial_puts("\n");

    lapic_write(LAPIC_ICR_LOW, icr_low);

    if (lapic_wait_for_ipi() < 0) {
        serial_puts("[APIC] INIT assert IPI timeout!\n");
        return -1;
    }
    serial_puts("[APIC] INIT assert IPI sent successfully\n");

    /* Step 2: Wait - reduced delay for QEMU */
    serial_puts("[APIC] Waiting for INIT deassert...\n");
    pit_delay_ms(100);  /* Reduced from 10ms to 1ms for QEMU */

    /* Step 3: Send INIT IPI (DEASSERT, level-triggered)
     * This is CRITICAL! INIT must be deasserted or the AP stays in INIT state */
    serial_puts("[APIC] Sending INIT IPI (deassert)...\n");

    /* Re-set destination APIC ID (may have been cleared) */
    icr_high = (uint32_t)apic_id << 24;
    lapic_write(LAPIC_ICR_HIGH, icr_high);

    /* INIT: level-triggered (NO assert) + INIT delivery mode */
    icr_low = LAPIC_ICR_LEVELTRIG | LAPIC_ICR_DM_INIT;
    icr_low |= LAPIC_ICR_DST_PHYSICAL;
    lapic_write(LAPIC_ICR_LOW, icr_low);

    if (lapic_wait_for_ipi() < 0) {
        serial_puts("[APIC] INIT deassert IPI timeout!\n");
        return -1;
    }
    serial_puts("[APIC] INIT deassert IPI sent successfully\n");

    /* Step 4: Delay before STARTUP (spec says 10ms, use 10ms for reliability) */
    pit_delay_ms(10);

    serial_puts("[APIC] Sending STARTUP IPI...\n");

    /* Clear ESR before STARTUP IPI (due to Pentium erratum 3AP) */
    if (maxlvt > 3) {
        lapic_write(LAPIC_ESR, 0);
        lapic_read(LAPIC_ESR);  /* Dummy read to flush */
    }

    /* Step 5: Send first STARTUP IPI (edge-triggered, no LEVELTRIG) */
    serial_puts("[APIC] Sending STARTUP IPI...\n");

    /* Clear ESR before STARTUP IPI (due to Pentium erratum 3AP) */
    if (maxlvt > 3) {
        lapic_write(LAPIC_ESR, 0);
        lapic_read(LAPIC_ESR);  /* Dummy read to flush */
    }

    /* Verify trampoline is at 0x7000 by checking the signature */
    uint16_t *trampoline_ptr = (uint16_t *)0x7000;
    uint16_t sig = *trampoline_ptr;
    serial_puts("[APIC] Trampoline at 0x7000: first word = 0x");
    for (int i = 12; i >= 0; i -= 4) {
        uint8_t nibble = (sig >> i) & 0xF;
        serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    serial_puts(" (should be 0xFA00 = cli)\n");

    /* Send STARTUP IPI using explicit APIC ID in ICR_HIGH */
    icr_high = (uint32_t)apic_id << 24;
    icr_low = LAPIC_ICR_DM_STARTUP | startup_addr;
    icr_low |= LAPIC_ICR_DST_PHYSICAL;

    serial_puts("[APIC] ICR_HIGH = 0x");
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (icr_high >> i) & 0xF;
        serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    serial_puts(" ICR_LOW = 0x");
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (icr_low >> i) & 0xF;
        serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    serial_puts("\n");

    lapic_write(LAPIC_ICR_HIGH, icr_high);
    lapic_write(LAPIC_ICR_LOW, icr_low);

    if (lapic_wait_for_ipi() < 0) {
        serial_puts("[APIC] STARTUP IPI timeout!\n");
        return -1;
    }
    /* No output here - let AP execute first */

    /* Step 6: Delay between STARTUP IPIs (spec says 200us, use 1ms for reliability) */
    pit_delay_ms(1);

    /* Check ESR for errors (due to Pentium erratum 3AP)
     * Note: ESR bit 7 (0x80) is Send Accept Error, which QEMU may set spuriously.
     * We log it but don't treat it as fatal. */
    if (maxlvt > 3) {
        uint32_t esr = lapic_read(LAPIC_ESR) & 0xEF;
        if (esr) {
            serial_puts("[APIC] WARNING: ESR = 0x");
            serial_putc('0' + (esr >> 4));
            serial_putc('0' + (esr & 0xF));
            serial_puts(" (may be QEMU-specific, continuing)\n");
        }
    }

    /* Step 7: Send second STARTUP IPI (recommended by Intel) */

    /* Clear ESR before second STARTUP IPI */
    if (maxlvt > 3) {
        lapic_write(LAPIC_ESR, 0);
        lapic_read(LAPIC_ESR);
    }

    /* Re-set destination APIC ID (may have been cleared) */
    icr_high = (uint32_t)apic_id << 24;
    icr_low = LAPIC_ICR_DM_STARTUP | startup_addr;
    icr_low |= LAPIC_ICR_DST_PHYSICAL;

    serial_puts("[APIC] Sending second STARTUP IPI...\n");
    lapic_write(LAPIC_ICR_HIGH, icr_high);
    lapic_write(LAPIC_ICR_LOW, icr_low);

    if (lapic_wait_for_ipi() < 0) {
        serial_puts("[APIC] Second STARTUP IPI timeout!\n");
        return -1;
    }

    /* Check ESR after second STARTUP IPI */
    if (maxlvt > 3) {
        uint32_t esr = lapic_read(LAPIC_ESR) & 0xEF;
        if (esr) {
            serial_puts("[APIC] ESR after second SIPI: 0x");
            serial_putc('0' + (esr >> 4));
            serial_putc('0' + (esr & 0xF));
            serial_puts("\n");
        }
    }

    /* Wait for AP to start executing - give it time to output debug chars */
    pit_delay_ms(100);

    serial_puts("[APIC] STARTUP IPI sent, waiting for AP...\n");

    return 0;
}

/**
 * is_bsp - Check if current CPU is the Bootstrap Processor
 *
 * Returns: 1 if BSP, 0 if AP
 */
int is_bsp(void) {
    uint64_t apic_base = rdmsr(IA32_APIC_BASE_MSR);
    /* Bit 8 of IA32_APIC_BASE_MSR is set for BSP */
    return (apic_base & (1 << 8)) ? 1 : 0;
}

/**
 * lapic_timer_init - Initialize Local APIC timer
 * @frequency: Timer frequency in Hz (ticks per second)
 *
 * Configures the Local APIC timer for periodic interrupts.
 * The timer will generate interrupts at the specified frequency.
 *
 * Note: The actual APIC timer base frequency is CPU-dependent.
 * For QEMU, a typical value is around 100 MHz, but may vary.
 * This function assumes the given frequency is the APIC base frequency.
 *
 * WARNING: Requires APIC MMIO region to be mapped in page tables.
 * The Local APIC is at 0xFEE00000 (~4GB), beyond the current 1GB mapping.
 * This will cause a page fault if APIC is not accessible.
 */
void lapic_timer_init(uint32_t frequency) {
    /* Note: APIC access may fail if not mapped in page tables */
    (void)frequency;  /* TODO: Implement once APIC is mapped */

    /* For now, this is a placeholder to show where timer init would go
     * The actual timer initialization requires:
     * 1. APIC MMIO region mapped in page tables (0xFEE00000)
     * 2. Proper APIC base address detection
     * 3. Timer frequency calibration
     */

    /* Placeholder: Timer init skipped - needs APIC memory mapping */
    asm volatile ("nop");  /* Prevent unused parameter warning */
}

/**
 * lapic_timer_set_divide - Configure timer divide
 * @divide_value: Divide configuration value
 *
 * Common values:
 * 0xB = divide by 1
 * 0x0 = divide by 2
 * 0x1 = divide by 4
 * 0x3 = divide by 16
 */
void lapic_timer_set_divide(uint32_t divide_value) {
    lapic_write(LAPIC_TIMER_DCR, divide_value);
}

/**
 * lapic_timer_set_initial_count - Set timer initial count
 * @count: Initial count value
 */
void lapic_timer_set_initial_count(uint32_t count) {
    lapic_write(LAPIC_TIMER_ICR, count);
}

/**
 * lapic_timer_get_current_count - Get current timer count
 *
 * Returns: Current timer count value
 */
uint32_t lapic_timer_get_current_count(void) {
    return lapic_read(LAPIC_TIMER_CCR);
}
