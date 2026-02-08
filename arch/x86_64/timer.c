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
#define INTERRUPTS_PER_QUOTE 10  /* Print quote every 10 interrupts (1 second at 10Hz) */

/* APIC Timer state */
volatile int apic_timer_count = 0;  /* Exported for test verification */
static volatile int apic_timer_active = 0;
static volatile int apic_timer_interrupt_count = 0;  /* Interrupt counter for delaying quotes */

/* External serial output functions */
extern void serial_puts(const char *str);
extern void serial_putc(char c);

/* Forward declarations */
void timer_stop(void);  /* Declared in timer.h, need forward declaration here */

/**
 * apic_timer_handler - APIC Timer interrupt handler
 *
 * Called from timer_isr in isr.S every time the APIC timer expires.
 * Increments a counter every INTERRUPTS_PER_QUOTE interrupts, up to NUM_QUOTES total.
 * The timer runs at high frequency (approx 1000Hz).
 */
void apic_timer_handler(void) {
    if (apic_timer_active) {
        /* Increment interrupt counter */
        apic_timer_interrupt_count++;

        /* Increment quote counter every INTERRUPTS_PER_QUOTE interrupts */
        if (apic_timer_interrupt_count >= INTERRUPTS_PER_QUOTE &&
            apic_timer_count < (int)NUM_QUOTES) {
            apic_timer_interrupt_count = 0;  /* Reset interrupt counter */

#if CONFIG_APIC_TIMER_TEST
            /* Optionally print a quote (disabled to avoid serial corruption in test mode) */
            /* serial_puts("[ APIC tests ]");
            serial_puts(math_quotes[apic_timer_count]);
            serial_puts("\n"); */
#endif
            apic_timer_count++;

            /* Stop timer after NUM_QUOTES - mask the hardware interrupt */
            if (apic_timer_count >= (int)NUM_QUOTES) {
                apic_timer_active = 0;
                timer_stop();  /* Mask the timer interrupt in hardware */
            }
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
#if CONFIG_APIC_TIMER_TEST
        serial_puts("[ APIC tests ]");
        serial_puts(math_quotes[apic_timer_count]);
        serial_puts("\n");
#endif
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
 * Resets the counters, unmask and start the APIC timer.
 */
void timer_start(void) {
    apic_timer_count = 0;
    apic_timer_interrupt_count = 0;  /* Reset interrupt counter */
    apic_timer_active = 1;

    /* Unmask and start the timer by setting initial count */
    extern uint32_t lapic_read(uint32_t offset);
    extern void lapic_write(uint32_t offset, uint32_t value);

    /* Unmask the timer */
    uint32_t lvt = lapic_read(0x320);  /* LAPIC_TIMER_LVT */
    lvt &= ~0x10000;  /* Clear mask bit */
    lapic_write(0x320, lvt);

    /* Set initial count to start the timer (use slower frequency for testing)
     * 10000000 = ~10Hz at 100MHz bus (much slower to avoid serial corruption) */
    lapic_write(0x380, 10000000);  /* LAPIC_TIMER_ICR */
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
