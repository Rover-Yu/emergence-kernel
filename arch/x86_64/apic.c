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
 * lapic_init - Initialize Local APIC
 */
void lapic_init(void) {
    uint64_t apic_base_msr;
    uint32_t ver;

    /* Read the IA32_APIC_BASE MSR to get actual APIC configuration */
    apic_base_msr = rdmsr(IA32_APIC_BASE_MSR);

    /* Check if APIC is enabled, enable it if not */
    if (!(apic_base_msr & IA32_APIC_BASE_ENABLED)) {
        apic_base_msr |= IA32_APIC_BASE_ENABLED;
        wrmsr(IA32_APIC_BASE_MSR, apic_base_msr);
    }

    /* Re-read MSR after potential modification */
    apic_base_msr = rdmsr(IA32_APIC_BASE_MSR);

    /* Use the actual APIC base address from MSR
     * Note: We're in long mode with paging enabled
     * The page tables should identity-map 0xFEE00000 -> 0xFEE00000
     * We can use the physical address directly because of the identity mapping */
    lapic_base = (volatile uint32_t *)0xFEE00000;

    /* CRITICAL FIX: Enable APIC via SVR BEFORE reading any registers!
     * The SVR bit 8 (software enable) must be set for APIC registers to be accessible.
     * Without this, all APIC register reads return 0. */

    /* Flush cache before write */
    asm volatile ("mfence" ::: "memory");

    lapic_write(LAPIC_SVR, 0x100 | 0xFF);  /* Enable + spurious vector */

    /* Flush cache after write */
    asm volatile ("mfence" ::: "memory");

    /* Memory fence to ensure SVR write completes before reading */
    asm volatile ("mfence" ::: "memory");

    /* Small delay to let APIC initialize */
    for (volatile int i = 0; i < 1000; i++) {
        asm volatile ("pause");
    }

    /* Read APIC version */
    ver = lapic_read(LAPIC_VER);
    serial_puts("APIC: LAPIC_VER = 0x");
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (ver >> i) & 0xF;
        serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    serial_puts("\n");
    serial_puts("APIC: APIC version=");
    serial_putc('0' + (ver & 0xFF));
    serial_puts(" maxlvt=");
    serial_putc('0' + ((ver >> 16) & 0xFF));
    serial_puts("\n");

    serial_puts("APIC: Local APIC initialized\n");
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

    while (timeout-- > 0) {
        icr_low = lapic_read(LAPIC_ICR_LOW);
        if (!(icr_low & LAPIC_ICR_DS)) {
            /* Delivery complete */
            return 0;
        }
        asm volatile ("pause");
    }

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

    /* Check APIC version (for ESR clearing) */
    ver = lapic_read(LAPIC_VER);
    maxlvt = (ver >> 16) & 0xFF;  /* Max LVT is in bits 16-23 */

    /* Be paranoid about clearing APIC errors (like Linux does)
     * Due to Pentium erratum 3AP */
    if (maxlvt > 3) {
        lapic_write(LAPIC_ESR, 0);
        lapic_read(LAPIC_ESR);  /* Dummy read to flush */
    }

    /* Step 1: Send INIT IPI (ASSERT, level-triggered) to reset the AP to real mode */
    icr_high = (uint32_t)apic_id << 24;
    lapic_write(LAPIC_ICR_HIGH, icr_high);
    icr_low = LAPIC_ICR_LEVELTRIG | LAPIC_ICR_ASSERT | LAPIC_ICR_DM_INIT;
    icr_low |= LAPIC_ICR_DST_PHYSICAL;
    lapic_write(LAPIC_ICR_LOW, icr_low);

    if (lapic_wait_for_ipi() < 0) {
        return -1;
    }

    /* Step 2: Wait - reduced delay for QEMU */
    pit_delay_ms(400);

    /* Step 3: Send INIT IPI (DEASSERT, level-triggered) */
    icr_high = (uint32_t)apic_id << 24;
    lapic_write(LAPIC_ICR_HIGH, icr_high);
    icr_low = LAPIC_ICR_LEVELTRIG | LAPIC_ICR_DM_INIT;
    icr_low |= LAPIC_ICR_DST_PHYSICAL;
    lapic_write(LAPIC_ICR_LOW, icr_low);

    if (lapic_wait_for_ipi() < 0) {
        return -1;
    }

    /* Step 4: Delay before STARTUP */
    pit_delay_ms(100);

    /* Clear ESR before STARTUP IPI */
    if (maxlvt > 3) {
        lapic_write(LAPIC_ESR, 0);
        lapic_read(LAPIC_ESR);
    }

    /* Step 5: Send first STARTUP IPI */
    icr_high = (uint32_t)apic_id << 24;
    icr_low = LAPIC_ICR_DM_STARTUP | startup_addr;
    icr_low |= LAPIC_ICR_DST_PHYSICAL;
    lapic_write(LAPIC_ICR_HIGH, icr_high);
    lapic_write(LAPIC_ICR_LOW, icr_low);

    if (lapic_wait_for_ipi() < 0) {
        return -1;
    }

    /* Step 6: Delay between STARTUP IPIs */
    pit_delay_ms(1);

    /* Step 7: Send second STARTUP IPI */
    if (maxlvt > 3) {
        lapic_write(LAPIC_ESR, 0);
        lapic_read(LAPIC_ESR);
    }

    icr_high = (uint32_t)apic_id << 24;
    icr_low = LAPIC_ICR_DM_STARTUP | startup_addr;
    icr_low |= LAPIC_ICR_DST_PHYSICAL;
    lapic_write(LAPIC_ICR_HIGH, icr_high);
    lapic_write(LAPIC_ICR_LOW, icr_low);

    if (lapic_wait_for_ipi() < 0) {
        return -1;
    }

    /* Wait for AP to start executing */
    pit_delay_ms(100);

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
 * apic_timer_init - Initialize APIC Timer for periodic interrupts
 *
 * Configures the Local APIC timer to generate periodic interrupts
 * at approximately 1000 Hz (1ms period).
 *
 * The APIC timer runs at the bus clock frequency. The actual frequency
 * varies by CPU, but dividing by 1 and using a reasonable initial count
 * gives us a predictable interrupt rate.
 */
void apic_timer_init(void) {
    uint32_t lvt_value;

    /* Set divide configuration register (divide by 1) */
    lapic_write(LAPIC_TIMER_DCR, LAPIC_TIMER_DIV_BY_1);

    /* Configure timer LVT (periodic mode) */
    lvt_value = TIMER_VECTOR;              /* Interrupt vector */
    lvt_value |= LAPIC_TIMER_LVT_PERIODIC; /* Periodic mode */
    lapic_write(LAPIC_TIMER_LVT, lvt_value);

    /* Set initial count to start the timer */
    lapic_write(LAPIC_TIMER_ICR, 100000);

    serial_puts("APIC: APIC timer initialized successfully\n");
}

