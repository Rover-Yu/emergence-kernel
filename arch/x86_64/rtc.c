/* JAKernel - x86-64 RTC implementation */

#include <stdint.h>
#include "arch/x86_64/rtc.h"
#include "arch/x86_64/io.h"

/* External serial output */
extern void serial_puts(const char *str);

/* Local RTC Status B bits for rtc_init() */
#define RTC_STATUS_B_SET     0x80   /* Set bit */
#define RTC_STATUS_B_DM      0x40   /* Data Mode bit */
#define RTC_STATUS_B_24HR    0x02   /* 24-hour format bit */

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
 */
void rtc_init(uint8_t rate) {
    uint8_t status_b;

    /* Disable RTC interrupts during setup */
    status_b = rtc_read(RTC_REG_STATUS_B);
    rtc_write(RTC_REG_STATUS_B, status_b & ~RTC_STATUS_B_SET);

    /* Enable binary mode (24-hour format is implicit in binary mode) */
    status_b = rtc_read(RTC_REG_STATUS_B);
    rtc_write(RTC_REG_STATUS_B, status_b | RTC_STATUS_B_DM);

    /* Set periodic interrupt rate */
    rtc_write(RTC_REG_STATUS_A, rate);

    /* Enable RTC interrupts (Status Register D, bit 7) */
    status_b = rtc_read(RTC_REG_STATUS_B);
    rtc_write(RTC_REG_STATUS_D, status_b | RTC_STATUS_D_IRQ);

    /* Read Status C to clear any pending flags */
    rtc_read(RTC_REG_STATUS_C);

    serial_puts("RTC initialized for periodic interrupts\n");
}

/* RTC interrupt count (for verification) */
static volatile int rtc_interrupt_count = 0;

/**
 * rtc_isr_handler - RTC interrupt handler
 *
 * Called from RTC ISR when RTC generates a periodic interrupt.
 * This is our timer interrupt handler for the demo.
 */
void rtc_isr_handler(void) {
    /* Clear interrupt flag by reading Status C */
    rtc_read(RTC_REG_STATUS_C);

    /* Call the generic timer handler */
    extern void timer_handler(void);
    timer_handler();

    rtc_interrupt_count++;
}