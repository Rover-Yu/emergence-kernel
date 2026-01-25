/* JAKernel - Local APIC Timer Driver */

#ifndef JAKERNEL_ARCH_X86_64_TIMER_H
#define JAKERNEL_ARCH_X86_64_TIMER_H

#include <stdint.h>

/* APIC Timer interrupt handler (called from timer_isr) */
void apic_timer_handler(void);

/* Legacy timer handler (for compatibility, deprecated) */
void timer_handler(void);

/* APIC Timer control functions */
void timer_start(void);
void timer_stop(void);
int timer_is_active(void);

#endif /* JAKERNEL_ARCH_X86_64_TIMER_H */
