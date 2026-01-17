/* JAKernel - Test Framework */

#ifndef JAKERNEL_TESTS_TEST_H
#define JAKERNEL_TESTS_TEST_H

#include <stdint.h>
#include <stdbool.h>

/* Timer test functions */

/**
 * timer_test_init - Initialize timer test
 *
 * Resets the timer counter and activates the timer for testing.
 */
void timer_test_init(void);

/**
 * timer_test_start - Start the timer test
 *
 * Activates the timer to print mathematician quotes.
 */
void timer_test_start(void);

/**
 * timer_test_stop - Stop the timer test
 */
void timer_test_stop(void);

/**
 * timer_test_is_active - Check if timer test is still active
 *
 * Returns: 1 if active, 0 if stopped
 */
int timer_test_is_active(void);

/* IPI test functions */

/**
 * ipi_test_init - Initialize IPI test
 *
 * Initializes the IPI driver for testing.
 */
int ipi_test_init(void);

/**
 * ipi_test_start - Start IPI self-test
 *
 * Sends 3 self-IPIs with math expressions.
 */
void ipi_test_start(void);

#endif /* JAKERNEL_TESTS_TEST_H */
