/* JAKernel - Timer Test Framework */

#include <stdint.h>
#include "tests/test.h"

/* Mathematician quotes (≤8 words each) */
static const char *math_quotes[] = {
    " 1. Mathematics is queen of sciences. - Gauss",
    " 2. Pure math is poetry of logic. - Einstein",
    " 3. Math reveals secrets to lovers. - Cantor",
    " 4. Proposing questions exceeds solving. - Cantor",
    " 5. God created natural numbers. - Kronecker"
};

#define NUM_QUOTES (sizeof(math_quotes) / sizeof(math_quotes[0]))

/* Timer test state */
static volatile int timer_test_count = 0;
static volatile int timer_test_active = 0;

/* External serial output functions */
extern void serial_puts(const char *str);

/**
 * timer_handler - Timer interrupt handler for test
 *
 * Called from RTC ISR when RTC generates a periodic interrupt.
 * Prints a mathematician quote every 0.5 seconds, up to 5 quotes total.
 */
void timer_handler(void) {
    if (timer_test_active && timer_test_count < (int)NUM_QUOTES) {
        serial_puts(math_quotes[timer_test_count]);
        serial_puts("\n");
        timer_test_count++;

        /* Stop timer after 5 quotes */
        if (timer_test_count >= (int)NUM_QUOTES) {
            timer_test_active = 0;
        }
    }
}

/**
 * timer_test_init - Initialize timer test
 *
 * Resets the timer counter and prepares for testing.
 */
void timer_test_init(void) {
    timer_test_count = 0;
    timer_test_active = 0;
}

/**
 * timer_test_start - Start the timer test
 *
 * Resets the counter and activates the timer.
 */
void timer_test_start(void) {
    timer_test_count = 0;
    timer_test_active = 1;
}

/**
 * timer_test_stop - Stop the timer test
 */
void timer_test_stop(void) {
    timer_test_active = 0;
}

/**
 * timer_test_is_active - Check if timer test is still active
 *
 * Returns: 1 if active, 0 if stopped
 */
int timer_test_is_active(void) {
    return timer_test_active;
}
