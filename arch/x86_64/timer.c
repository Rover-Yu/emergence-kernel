/* JAKernel - Local APIC Timer Driver */

#include <stdint.h>
#include "arch/x86_64/timer.h"

/* Mathematician quotes (≤8 words each) */
static const char *math_quotes[] = {
    " 1. Mathematics is queen of sciences. - Gauss",
    " 2. Pure math is poetry of logic. - Einstein",
    " 3. Math reveals secrets to lovers. - Cantor",
    " 4. Proposing questions exceeds solving. - Cantor",
    " 5. God created natural numbers. - Kronecker"
};

#define NUM_QUOTES (sizeof(math_quotes) / sizeof(math_quotes[0]))

/* Timer state */
static volatile int timer_count = 0;
static volatile int timer_active = 0;

/* External serial output functions */
extern void serial_puts(const char *str);
extern void serial_putc(char c);

/**
 * timer_handler - Timer interrupt handler
 *
 * Called from timer_isr in isr.S every time the timer expires.
 * Prints a mathematician quote every second, up to 5 quotes total.
 */
void timer_handler(void) {
    serial_puts("[DEBUG: timer_handler called, count=");
    serial_putc('0' + timer_count);
    serial_puts("]\n");

    if (timer_active && timer_count < (int)NUM_QUOTES) {
        serial_puts(math_quotes[timer_count]);
        serial_puts("\n");
        timer_count++;

        /* Stop timer after 5 quotes */
        if (timer_count >= (int)NUM_QUOTES) {
            timer_active = 0;
            serial_puts("[DEBUG: Timer stopped]\n");
        }
    }
}

/**
 * timer_start - Start the timer for quote printing
 *
 * Resets the counter and activates the timer.
 */
void timer_start(void) {
    timer_count = 0;
    timer_active = 1;
}

/**
 * timer_stop - Stop the timer
 */
void timer_stop(void) {
    timer_active = 0;
}

/**
 * timer_is_active - Check if timer is still active
 *
 * Returns: 1 if active, 0 if stopped
 */
int timer_is_active(void) {
    return timer_active;
}
