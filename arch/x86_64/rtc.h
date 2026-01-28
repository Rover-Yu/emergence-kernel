/* Emergence Kernel - x86-64 RTC (Real Time Clock) for periodic interrupts */

#ifndef EMERGENCE_ARCH_X86_64_RTC_H
#define EMERGENCE_ARCH_X86_64_RTC_H

#include <stdint.h>

/* RTC I/O Ports */
#define RTC_INDEX         0x70
#define RTC_DATA          0x71

/* RTC Registers */
#define RTC_REG_STATUS_A      0x0A
#define RTC_REG_STATUS_B      0x0B
#define RTC_REG_STATUS_C      0x0C

/* RTC Status B bits */
#define RTC_STATUS_B_DM      0x80   /* Data Mode (1 = binary) */
#define RTC_STATUS_B_24HR    0x02   /* 24-hour format */

/* RTC Status C bits */
#define RTC_STATUS_C_PF     0x40   /* Periodic Flag */
#define RTC_STATUS_C_UIP    0x10   /* Update in Progress */

/* RTC Periodic Interrupt Rates */
#define RTC_RATE_2HZ        0x0F   /* 2 Hz (500ms period) - Good for demo! */
#define RTC_RATE_4HZ        0x0E   /* 4 Hz (250ms period) */
#define RTC_RATE_8HZ        0x0D   /* 8 Hz (125ms period) */
#define RTC_RATE_16HZ       0x0C   /* 16 Hz (62.5ms period) */

/* Function prototypes */

/* Initialize RTC for periodic interrupts */
void rtc_init(uint8_t rate);

/* Read RTC register */
uint8_t rtc_read(uint8_t reg);

/* Write RTC register */
void rtc_write(uint8_t reg, uint8_t value);

#endif /* EMERGENCE_ARCH_X86_64_RTC_H */
