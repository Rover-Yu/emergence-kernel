/* JAKernel - x86-64 Local APIC implementation */

#include <stdint.h>
#include "arch/x86_64/apic.h"
#include "arch/x86_64/io.h"

/* Local APIC base address (set during initialization) */
static volatile uint32_t *lapic_base = (volatile uint32_t *)LAPIC_DEFAULT_BASE;

/* IA32_APIC_BASE MSR */
#define IA32_APIC_BASE_MSR  0x1B

/* MSR read/write functions */
static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    asm volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

/**
 * lapic_read - Read Local APIC register
 * @offset: Register offset from base
 *
 * Returns: Register value
 */
uint32_t lapic_read(uint32_t offset) {
    return lapic_base[offset / 4];
}

/**
 * lapic_write - Write Local APIC register
 * @offset: Register offset from base
 * @value: Value to write
 */
void lapic_write(uint32_t offset, uint32_t value) {
    lapic_base[offset / 4] = value;
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
    /* For now, use the default Local APIC base address
     * Reading from MSR might fail if APIC is not enabled yet */
    lapic_base = (volatile uint32_t *)LAPIC_DEFAULT_BASE;

    /* Enable Local APIC via SVR register */
    uint32_t svr = lapic_read(LAPIC_SVR);
    svr |= LAPIC_SVR_ENABLE;  /* Set bit 8 to enable APIC */
    svr &= 0xFFFFFF00;        /* Clear spurious vector */
    lapic_write(LAPIC_SVR, svr);
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
    icr_low |= LAPIC_ICR_ASSERT;        /* Assert interrupt */

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
    int timeout = 10000;
    while ((lapic_read(LAPIC_ICR_LOW) & LAPIC_ICR_DS) && timeout-- > 0) {
        asm volatile ("pause");
    }
    return (timeout < 0) ? -1 : 0;
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
 */
int ap_startup(uint8_t apic_id, uint32_t startup_addr) {
    /* Send INIT IPI to reset the AP */
    lapic_send_ipi(apic_id, LAPIC_ICR_DM_INIT, 0);
    lapic_wait_for_ipi();

    /* Small delay (10ms) */
    for (int i = 0; i < 1000000; i++) {
        asm volatile ("pause");
    }

    /* Send STARTUP IPI (must send twice per spec) */
    lapic_send_ipi(apic_id, LAPIC_ICR_DM_STARTUP, startup_addr);
    lapic_wait_for_ipi();

    /* Small delay */
    for (int i = 0; i < 200; i++) {
        asm volatile ("pause");
    }

    /* Send second STARTUP IPI */
    lapic_send_ipi(apic_id, LAPIC_ICR_DM_STARTUP, startup_addr);
    lapic_wait_for_ipi();

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
