/* Emergence Kernel - Local APIC Timer Driver */

#include <stdint.h>
#include "arch/x86_64/timer.h"
#include "arch/x86_64/apic.h"

/* Mathematician quotes (≤8 words each) */
static const char *math_quotes[] = {
    " 1. Mathematics is queen of sciences. - Gauss",
    " 2. Pure math is poetry of logic. - Einstein",
    " 3. Math reveals secrets to lovers. - Cantor",
    " 4. Proposing questions exceeds solving. - Cantor",
    " 5. God created natural numbers. - Kronecker"
};

#define NUM_QUOTES (sizeof(math_quotes) / sizeof(math_quotes[0]))

/* APIC Timer state */
static volatile int apic_timer_count = 0;
static volatile int apic_timer_active = 0;

/* External serial output functions */
extern void serial_puts(const char *str);
extern void serial_putc(char c);

/* Forward declarations */
void timer_stop(void);  /* Declared in timer.h, need forward declaration here */

/**
 * apic_timer_handler - APIC Timer interrupt handler
 *
 * Called from timer_isr in isr.S every time the APIC timer expires.
 * Prints a mathematician quote every 0.5 seconds, up to 5 quotes total.
 * The timer runs at high frequency (approx 1000Hz).
 */
void apic_timer_handler(void) {
    if (apic_timer_active && apic_timer_count < (int)NUM_QUOTES) {
        serial_puts("[ APIC tests ]");
        serial_puts(math_quotes[apic_timer_count]);
        serial_puts("\n");
        apic_timer_count++;

        /* Stop timer after 5 quotes - mask the hardware interrupt */
        if (apic_timer_count >= (int)NUM_QUOTES) {
            apic_timer_active = 0;
            timer_stop();  /* Mask the timer interrupt in hardware */
        }
    }
}

/**
 * timer_handler - Legacy timer interrupt handler (for compatibility)
 *
 * This is the original handler that is now deprecated.
 * Use apic_timer_handler instead.
 */
void timer_handler(void) {
    if (apic_timer_active && apic_timer_count < (int)NUM_QUOTES) {
        serial_puts("[ APIC tests ]");
        serial_puts(math_quotes[apic_timer_count]);
        serial_puts("\n");
        apic_timer_count++;

        /* Stop timer after 5 quotes - mask the hardware interrupt */
        if (apic_timer_count >= (int)NUM_QUOTES) {
            apic_timer_active = 0;
            timer_stop();  /* Mask the timer interrupt in hardware */
        }
    }
}

/**
 * timer_start - Start the APIC timer for quote printing
 *
 * Resets the counter and activates the APIC timer.
 */
void timer_start(void) {
    apic_timer_count = 0;
    apic_timer_active = 1;
}

/**
 * timer_stop - Stop the APIC timer
 *
 * Masks the timer interrupt to stop periodic interrupts.
 */
void timer_stop(void) {
    /* Mask the timer interrupt in the LVT */
    /* Write the timer vector with the mask bit set */
    extern uint32_t lapic_read(uint32_t offset);
    extern void lapic_write(uint32_t offset, uint32_t value);

    lapic_write(0x320, TIMER_VECTOR | 0x10000);  /* Mask timer interrupt */
    apic_timer_active = 0;
}

/**
 * timer_is_active - Check if APIC timer is still active
 *
 * Returns: 1 if active, 0 if stopped
 */
int timer_is_active(void) {
    return apic_timer_active;
}
