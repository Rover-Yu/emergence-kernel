/* Emergence Kernel - Monitor Trampoline Test Wrapper Header */

#ifndef TEST_MONITOR_TRAMPOLINE_H
#define TEST_MONITOR_TRAMPOLINE_H

/**
 * test_monitor_trampoline - Test monitor trampoline CR3 switching
 *
 * Wrapper function that handles CONFIG guard and runtime test selection.
 * Calls test_monitor_call_from_unprivileged() if CONFIG_TESTS_NK_TRAMPOLINE
 * is enabled and test is selected.
 */
void test_monitor_trampoline(void);

#endif /* TEST_MONITOR_TRAMPOLINE_H */
