/* Emergence Kernel - NK Invariants Verify Test
 * Verifies Nested Kernel invariants after CR3 switch on BSP and APs */

#include "test_nk_invariants_verify.h"
#include "kernel/test.h"
#include "kernel/klog.h"
#include "arch/x86_64/serial.h"

/* External monitor function for invariant verification */
extern void monitor_verify_invariants(void);

#if CONFIG_TESTS_NK_INVARIANTS_VERIFY

/**
 * test_nk_invariants_verify - Verify NK invariants on BSP after CR3 switch
 */
void test_nk_invariants_verify(void) {
    klog_debug("NK_VERIFY_TEST", "BSP: Verifying Nested Kernel invariants...");
    monitor_verify_invariants();
    klog_debug("NK_VERIFY_TEST", "BSP: Invariants verified successfully");
}

/**
 * test_nk_invariants_verify_ap - Verify NK invariants on AP after CR3 switch
 */
void test_nk_invariants_verify_ap(void) {
    klog_debug("NK_VERIFY_TEST", "AP: Verifying Nested Kernel invariants...");
    monitor_verify_invariants();
    klog_debug("NK_VERIFY_TEST", "AP: Invariants verified successfully");
}

#else /* !CONFIG_TESTS_NK_INVARIANTS_VERIFY */

void test_nk_invariants_verify(void) { }
void test_nk_invariants_verify_ap(void) { }

#endif /* CONFIG_TESTS_NK_INVARIANTS_VERIFY */
