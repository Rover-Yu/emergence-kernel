/* Emergence Kernel - NK Monitor Trampoline Test Wrapper Header */

#ifndef TEST_NK_MONITOR_TRAMPOLINE_H
#define TEST_NK_MONITOR_TRAMPOLINE_H

/**
 * test_nk_monitor_trampoline - Test monitor trampoline CR0.WP toggle
 *
 * With the CR0.WP toggle design, the monitor trampoline:
 * 1. Saves current CR0.WP state to per-CPU data
 * 2. Clears CR0.WP (bit 16) to enter NK mode (allows writes to read-only pages)
 * 3. Calls the monitor handler
 * 4. Restores CR0.WP to return to OK mode (enforces write protection)
 *
 * This test verifies:
 * - CR0.WP is correctly toggled during monitor calls
 * - CR0.WP is restored to 1 after monitor calls
 * - Monitor calls work correctly from OK mode
 * - Multiple sequential monitor calls work correctly
 *
 * Wrapper function that handles CONFIG guard and runtime test selection.
 * Calls test_cr0_wp_toggle() and test_monitor_call_from_unprivileged()
 * if CONFIG_TESTS_NK_TRAMPOLINE is enabled.
 */
void test_nk_monitor_trampoline(void);

#endif /* TEST_NK_MONITOR_TRAMPOLINE_H */
