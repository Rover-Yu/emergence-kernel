/* JAKernel - x86-64 RTC implementation */

#include <stdint.h>
#include "arch/x86_64/rtc.h"
#include "arch/x86_64/io.h"

/* External serial output */
extern void serial_puts(const char *str);

/* Use RTC Status B definitions from rtc.h */

/**
 * rtc_read - Read RTC register
 * @reg: Register number
 *
 * Returns: Register value
 */
uint8_t rtc_read(uint8_t reg) {
    outb(RTC_INDEX, reg);
    return inb(RTC_DATA);
}

/**
 * rtc_write - Write RTC register
 * @reg: Register number
 * @value: Value to write
 */
void rtc_write(uint8_t reg, uint8_t value) {
    outb(RTC_INDEX, reg);
    outb(RTC_DATA, value);
}

/**
 * rtc_init - Initialize RTC for periodic interrupts
 * @rate: Interrupt rate (use RTC_RATE_* constants)
 *
 * Configures the RTC to generate periodic interrupts at the specified rate.
 * Common rates: 2Hz, 4Hz, 8Hz, 16Hz, etc.
 *
 * Note: RTC uses IRQ 8, which needs to be enabled in the PIC.
 * For simplicity, we assume the RTC is connected to IRQ 8.
 *
 * RTC Status A register format:
 *   Bits 0-3: Rate selector (what we set)
 *   Bits 4-6: Divider (must preserve, usually 0110 = 32.768kHz)
 *   Bit 7:    Update in progress (read-only)
 */
void rtc_init(uint8_t rate) {
    uint8_t status_a, status_b;

    /* Set periodic interrupt rate
     * IMPORTANT: We must preserve bits 4-7 of STATUS_A!
     * Only the lower 4 bits (0-3) are for the rate selector.
     * The divider bits (4-6) should typically be 011 (32.768kHz base).
     */
    status_a = rtc_read(RTC_REG_STATUS_A);
    /* Clear lower 4 bits (rate field) and set new rate */
    status_a = (status_a & 0xF0) | (rate & 0x0F);
    rtc_write(RTC_REG_STATUS_A, status_a);

    /* Enable RTC interrupts (Status Register B, bit 6 = Periodic Interrupt Enable) */
    status_b = rtc_read(RTC_REG_STATUS_B);
    rtc_write(RTC_REG_STATUS_B, status_b | 0x40);  /* Set bit 6 (PIE = Periodic Interrupt Enable) */

    /* Read Status C to clear any pending flags */
    rtc_read(RTC_REG_STATUS_C);

    serial_puts("RTC initialized for periodic interrupts\n");
}

/* Note: RTC timer functionality removed - only APIC timer is supported */

/**
 * rtc_isr_handler - RTC interrupt handler (stub)
 *
 * RTC timer functionality has been removed. Only APIC timer is supported.
 * This function is kept as a stub but should not be used.
 */
void rtc_isr_handler(void) {
    /* Clear interrupt flag by reading Status C */
    rtc_read(RTC_REG_STATUS_C);

    /* RTC timer functionality removed - do nothing */
}