/* Emergence Kernel - NK Monitor Trampoline Test Wrapper Header */

#ifndef TEST_NK_MONITOR_TRAMPOLINE_H
#define TEST_NK_MONITOR_TRAMPOLINE_H

/**
 * test_nk_monitor_trampoline - Test monitor trampoline CR3 switching
 *
 * Wrapper function that handles CONFIG guard and runtime test selection.
 * Calls test_monitor_call_from_unprivileged() if CONFIG_TESTS_NK_TRAMPOLINE
 * is enabled and test is selected.
 */
void test_nk_monitor_trampoline(void);

#endif /* TEST_NK_MONITOR_TRAMPOLINE_H */
