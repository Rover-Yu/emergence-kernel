/* Emergence Kernel - Spinlock Test Wrapper Header */

#ifndef TEST_SPINLOCK_H
#define TEST_SPINLOCK_H

/**
 * test_spinlock_bsp_setup - BSP setup for spinlock tests
 *
 * Initializes spinlock_test_start flag and runs tests.
 * Must be called by BSP after AP startup is complete.
 */
void test_spinlock_bsp_setup(void);

/**
 * test_spinlock_ap_entry - AP entry point for spinlock tests
 *
 * Called by APs when spinlock_test_start is set.
 * APs participate in SMP tests coordinated by BSP.
 */
void test_spinlock_ap_entry(void);

#endif /* TEST_SPINLOCK_H */
