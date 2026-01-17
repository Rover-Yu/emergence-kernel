/* JAKernel - x86-64 8259 PIT (Programmable Interval Timer) */

#ifndef JAKERNEL_ARCH_X86_64_PIT_H
#define JAKERNEL_ARCH_X86_64_PIT_H

#include <stdint.h>

/* PIT I/O Ports */
#define PIT_CH0_DATA      0x40   /* Channel 0 data port */
#define PIT_COMMAND      0x43   /* Command register */

/* PIT Command Bits */
#define PIT_CHANNEL_0     0x00   /* Select channel 0 */
#define PIT_ACCESS_LATCH  0x00   /* Latch count value */
#define PIT_ACCESS_LOHI  0x30   /* Access mode: low then high byte */
#define PIT_MODE_SQUARE  0x06   /* Square wave mode */
#define PIT_FREQ_DIVISOR  0x00   /* Use 16-bit binary count */

/* PIT Frequency */
#define PIT_FREQUENCY    1193182 /* Base frequency (1.193182 MHz) */

/* Function prototypes */

/* Initialize PIT for periodic interrupts */
void pit_init(uint32_t frequency);

/* Read current counter value */
uint16_t pit_read_counter(void);

#endif /* JAKERNEL_ARCH_X86_64_PIT_H */
