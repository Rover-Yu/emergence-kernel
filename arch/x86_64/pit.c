/* JAKernel - x86-64 8259 PIT implementation */

#include <stdint.h>
#include "arch/x86_64/pit.h"
#include "arch/x86_64/io.h"

/**
 * pit_init - Initialize 8259 PIT for periodic interrupts
 * @frequency: Desired interrupt frequency in Hz
 *
 * Configures channel 0 of the PIT in square wave mode.
 * The frequency must be in the range: PIT_FREQUENCY / 65536 <= freq <= PIT_FREQUENCY
 *
 * Common values:
 * - 100 Hz = 10ms period
 * - 1000 Hz = 1ms period
 * - 1193182 Hz = ~0.84us period
 */
void pit_init(uint32_t frequency) {
    /* Calculate divisor */
    uint16_t divisor = PIT_FREQUENCY / frequency;

    /* Send command to PIT:
     * Bits 7-6: Channel 0
     * Bits 5-4: Access mode (low/high byte)
     * Bits 3-1: Mode 3 (square wave)
     * Bit 0: Binary counting
     */
    uint8_t command = PIT_CHANNEL_0 | PIT_ACCESS_LOHI | PIT_MODE_SQUARE | PIT_FREQ_DIVISOR;
    outb(PIT_COMMAND, command);

    /* Send divisor (low byte, then high byte) */
    outb(PIT_CH0_DATA, divisor & 0xFF);
    outb(PIT_CH0_DATA, (divisor >> 8) & 0xFF);
}

/**
 * pit_read_counter - Read current PIT counter value
 *
 * Returns: Current counter value (decrements from divisor to 0)
 */
uint16_t pit_read_counter(void) {
    uint8_t lo, hi;

    /* Latch the count */
    outb(PIT_COMMAND, PIT_CHANNEL_0 | PIT_ACCESS_LATCH);

    /* Read low byte */
    lo = inb(PIT_CH0_DATA);

    /* Read high byte */
    hi = inb(PIT_CH0_DATA);

    return ((uint16_t)hi << 8) | lo;
}
