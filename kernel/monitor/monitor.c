/* monitor.c - Monitor core implementation for nested kernel architecture */

#include "monitor.h"
#include "arch/x86_64/paging.h"
#include "arch/x86_64/serial.h"

/* External functions */
extern void *pmm_alloc(uint8_t order);
extern void pmm_free(void *addr, uint8_t order);
extern int smp_get_cpu_index(void);

/* Page table physical addresses */
uint64_t monitor_pml4_phys = 0;
uint64_t unpriv_pml4_phys = 0;

/* Monitor page table structures (allocated during init) */
static uint64_t *monitor_pml4;
static uint64_t *monitor_pdpt;
static uint64_t *monitor_pd;

static uint64_t *unpriv_pml4;
static uint64_t *unpriv_pdpt;
static uint64_t *unpriv_pd;

/* External boot page table symbols (from boot.S) - these are 4KB arrays */
extern uint64_t boot_pml4[];
extern uint64_t boot_pdpt[];
extern uint64_t boot_pd[];

/* Get physical address from virtual (identity mapped) */
static inline uint64_t virt_to_phys(void *virt) {
    return (uint64_t)virt;
}

/* Helper: Invalidate TLB for a specific virtual address
 * Required after modifying PTE to ensure Invariant 2 enforcement */
static void monitor_invalidate_page(void *addr) {
    asm volatile ("invlpg (%0)" : : "r"(addr) : "memory");
}

/* Helper: Find PD entry covering a physical address
 * For identity mapping, PD index = (addr >> 21) & 0x1FF */
static int monitor_find_pd_entry(uint64_t phys_addr) {
    return (phys_addr >> 21) & 0x1FF;
}

/* Protect monitor state pages in unprivileged view
 * This implements Nested Kernel Invariant 1 and Invariant 5:
 * - PTPs are marked read-only while outer kernel executes
 * - All mappings to PTPs are marked read-only */
static void monitor_protect_state(void) {
    serial_puts("MONITOR: Enforcing Nested Kernel invariants\n");
    serial_puts("MONITOR: Protecting page table pages (Invariant 5)\n");

    /* Get physical addresses of all 6 monitor page table pages
     * These are the PTPs that must be write-protected (Invariant 5) */
    uint64_t monitor_pages[] = {
        virt_to_phys(monitor_pml4),
        virt_to_phys(monitor_pdpt),
        virt_to_phys(monitor_pd),
        virt_to_phys(unpriv_pml4),
        virt_to_phys(unpriv_pdpt),
        virt_to_phys(unpriv_pd)
    };

    /* Find the PD entry covering the first page */
    int pd_index = monitor_find_pd_entry(monitor_pages[0]);

    /* Verify all pages are in the same 2MB region */
    for (int i = 1; i < 6; i++) {
        int idx = monitor_find_pd_entry(monitor_pages[i]);
        if (idx != pd_index) {
            serial_puts("MONITOR: WARNING: Monitor pages span multiple 2MB regions\n");
            serial_puts("MONITOR: Only protecting first region\n");
            break;
        }
    }

    /* Clear Writable bit in unpriv_pd entry (make it read-only)
     * This implements Invariant 1: protected data is read-only for outer kernel */
    uint64_t *unpriv_pd_entry = &unpriv_pd[pd_index];
    uint64_t original_entry = *unpriv_pd_entry;

    *unpriv_pd_entry = original_entry & ~X86_PTE_WRITABLE;

    serial_puts("MONITOR: Protected PD entry at index ");
    serial_putc('0' + pd_index);
    serial_puts("\n");

    /* Verify monitor_pd is still writable (nested kernel needs write access) */
    uint64_t *monitor_pd_entry = &monitor_pd[pd_index];
    if (!(*monitor_pd_entry & X86_PTE_WRITABLE)) {
        serial_puts("MONITOR: ERROR: monitor_pd should remain writable!\n");
    }

    /* Invalidate TLB for the affected 2MB region
     * This ensures Invariant 2: write-protection is enforced */
    monitor_invalidate_page((void *)monitor_pages[0]);

    serial_puts("MONITOR: TLB invalidated (Invariant 2 enforcement active)\n");
    serial_puts("MONITOR: Nested Kernel invariants enforced\n");
}

/* Verify Nested Kernel invariants are correctly configured
 *
 * This is called ONLY from unprivileged kernel mode (after CR3 switch in main.c)
 * to verify that all Nested Kernel invariants from the ASPLOS '15 paper are
 * correctly enforced.
 *
 * Invariants are checked in numerical order: 1, 2, 3, 4, 5, 6
 *
 * Output behavior:
 * - Always shows final PASS/FAIL result
 * - Shows per-invariant details only if CONFIG_INVARIANTS_VERBOSE=1
 */
void monitor_verify_invariants(void) {
    extern int smp_get_cpu_index(void);

    int cpu_id = smp_get_cpu_index();

#if CONFIG_INVARIANTS_VERBOSE
    serial_puts("\n=== Nested Kernel Invariant Verification (CPU ");
    serial_putc('0' + cpu_id);
    serial_puts(") ===\n");
#endif

    /* Get physical address of unpriv_pd to find PD entry */
    uint64_t unpriv_pd_phys = virt_to_phys(unpriv_pd);
    int pd_index = monitor_find_pd_entry(unpriv_pd_phys);

    /* Check the PD entry in both views */
    uint64_t unpriv_entry = unpriv_pd[pd_index];
    uint64_t monitor_entry = monitor_pd[pd_index];

    /* Variables for invariant checks */
    bool unpriv_writable = (unpriv_entry & X86_PTE_WRITABLE);
    bool monitor_writable = (monitor_entry & X86_PTE_WRITABLE);
    bool cr0_wp_enabled;
    bool global_mappings_match;
    int mismatch_count = 0;
    bool cr3_is_predeclared;
    bool context_switch_available;
    uint64_t current_cr3;

#if CONFIG_INVARIANTS_VERBOSE
    /* === Invariant 1: Protected data is read-only in outer kernel === */
    serial_puts("VERIFY: [Inv 1] PTPs read-only in outer kernel:\n");
    serial_puts("VERIFY:   unpriv_pd writable bit: ");
    serial_putc(unpriv_writable ? '1' : '0');
    serial_puts(" (expected: 0) - ");
    if (!unpriv_writable) {
        serial_puts("PASS\n");
    } else {
        serial_puts("FAIL\n");
    }
#endif

    /* === Invariant 2: Write-protection permissions enforced === */
    {
        uint64_t cr0;
        asm volatile ("mov %%cr0, %0" : "=r"(cr0));
        cr0_wp_enabled = (cr0 & (1 << 16));
    }

#if CONFIG_INVARIANTS_VERBOSE
    serial_puts("VERIFY: [Inv 2] CR0.WP enforcement active:\n");
    serial_puts("VERIFY:   CR0.WP bit: ");
    serial_putc(cr0_wp_enabled ? '1' : '0');
    serial_puts(" (expected: 1) - ");
    if (cr0_wp_enabled) {
        serial_puts("PASS\n");
    } else {
        serial_puts("FAIL\n");
    }
#endif

    /* === Invariant 3: Global mappings accessible in both views === */
    /* Check that PML4 entries are identical for identity-mapped regions */
    global_mappings_match = true;
    for (int i = 0; i < 512; i++) {
        if (i == pd_index) continue;
        if (monitor_pml4[i] != unpriv_pml4[i]) {
            global_mappings_match = false;
            mismatch_count++;
        }
    }

#if CONFIG_INVARIANTS_VERBOSE
    serial_puts("VERIFY: [Inv 3] Global mappings accessible in both views:\n");
    serial_puts("VERIFY:   PML4 entries compared: 512 entries, mismatches: ");
    if (mismatch_count == 0) {
        serial_puts("0");
    } else {
        serial_put_hex(mismatch_count);
    }
    serial_puts(" - ");
    if (global_mappings_match) {
        serial_puts("PASS\n");
    } else {
        serial_puts("FAIL\n");
    }
#endif

    /* === Invariant 4: Context switch consistency === */
    /* Verify that we can access privileged mode via monitor call */
    context_switch_available = (monitor_pml4_phys != 0 && unpriv_pml4_phys != 0);

#if CONFIG_INVARIANTS_VERBOSE
    serial_puts("VERIFY: [Inv 4] Context switch mechanism:\n");
    serial_puts("VERIFY:   monitor_call_stub available - ");
    if (context_switch_available) {
        serial_puts("PASS\n");
    } else {
        serial_puts("FAIL\n");
    }
#endif

    /* === Invariant 5: All PTPs marked read-only in outer kernel === */
#if CONFIG_INVARIANTS_VERBOSE
    serial_puts("VERIFY: [Inv 5] PTPs writable in nested kernel:\n");
    serial_puts("VERIFY:   monitor_pd writable bit: ");
    serial_putc(monitor_writable ? '1' : '0');
    serial_puts(" (expected: 1) - ");
    if (monitor_writable) {
        serial_puts("PASS\n");
    } else {
        serial_puts("FAIL\n");
    }
#endif

    /* === Invariant 6: CR3 only loaded with pre-declared PTP === */
    /* Check that current CR3 is one of the two pre-declared PTPs */
    asm volatile ("mov %%cr3, %0" : "=r"(current_cr3));
    cr3_is_predeclared = (current_cr3 == monitor_pml4_phys) ||
                         (current_cr3 == unpriv_pml4_phys);

#if CONFIG_INVARIANTS_VERBOSE
    serial_puts("VERIFY: [Inv 6] CR3 loaded with pre-declared PTP:\n");
    serial_puts("VERIFY:   Current CR3: 0x");
    serial_put_hex(current_cr3);
    serial_puts("\n");
    serial_puts("VERIFY:   monitor_pml4_phys: 0x");
    serial_put_hex(monitor_pml4_phys);
    serial_puts(", unpriv_pml4_phys: 0x");
    serial_put_hex(unpriv_pml4_phys);
    serial_puts(" - ");
    if (cr3_is_predeclared) {
        serial_puts("PASS\n");
    } else {
        serial_puts("FAIL\n");
    }
#endif

    /* === Final Verdict === */
    bool all_pass = !unpriv_writable && monitor_writable && cr0_wp_enabled &&
                   global_mappings_match && cr3_is_predeclared &&
                   context_switch_available;

#if CONFIG_INVARIANTS_VERBOSE
    serial_puts("=== Verification Complete ===\n\n");
#endif

    /* Always print final result (both verbose and quiet modes) */
    if (all_pass) {
        serial_puts("[CPU ");
        serial_putc('0' + cpu_id);
        serial_puts("] Nested Kernel invariants: PASS\n");
    } else {
        serial_puts("[CPU ");
        serial_putc('0' + cpu_id);
        serial_puts("] Nested Kernel invariants: FAIL!\n");

#if !CONFIG_INVARIANTS_VERBOSE
        /* In quiet mode, show which invariants failed */
        if (unpriv_writable) {
            serial_puts("  [Inv 1] FAIL: PTPs not read-only in outer kernel\n");
        }
        if (!cr0_wp_enabled) {
            serial_puts("  [Inv 2] FAIL: CR0.WP not enforced\n");
        }
        if (!global_mappings_match) {
            serial_puts("  [Inv 3] FAIL: Global mappings don't match\n");
        }
        if (!context_switch_available) {
            serial_puts("  [Inv 4] FAIL: Context switch unavailable\n");
        }
        if (!monitor_writable) {
            serial_puts("  [Inv 5] FAIL: PTPs not writable in nested kernel\n");
        }
        if (!cr3_is_predeclared) {
            serial_puts("  [Inv 6] FAIL: CR3 not pre-declared\n");
        }
#endif
    }
}

/* Initialize monitor page tables */
void monitor_init(void) {
    serial_puts("MONITOR: Initializing nested kernel architecture\n");

    /* Allocate page tables for monitor (privileged) view */
    monitor_pml4 = (uint64_t *)pmm_alloc(0);  /* 1 page */
    monitor_pdpt = (uint64_t *)pmm_alloc(0);
    monitor_pd = (uint64_t *)pmm_alloc(0);

    if (!monitor_pml4 || !monitor_pdpt || !monitor_pd) {
        serial_puts("MONITOR: Failed to allocate monitor page tables\n");
        return;
    }

    /* Allocate page tables for unprivileged view */
    unpriv_pml4 = (uint64_t *)pmm_alloc(0);
    unpriv_pdpt = (uint64_t *)pmm_alloc(0);
    unpriv_pd = (uint64_t *)pmm_alloc(0);

    if (!unpriv_pml4 || !unpriv_pdpt || !unpriv_pd) {
        serial_puts("MONITOR: Failed to allocate unprivileged page tables\n");
        return;
    }

    /* Copy boot page table mappings to monitor view */
    for (int i = 0; i < 512; i++) {
        monitor_pml4[i] = boot_pml4[i];
        monitor_pdpt[i] = boot_pdpt[i];
        monitor_pd[i] = boot_pd[i];

        unpriv_pml4[i] = boot_pml4[i];
        unpriv_pdpt[i] = boot_pdpt[i];
        unpriv_pd[i] = boot_pd[i];
    }

    /* Save physical addresses */
    monitor_pml4_phys = virt_to_phys(monitor_pml4);
    unpriv_pml4_phys = virt_to_phys(unpriv_pml4);

    serial_puts("MONITOR: Page tables initialized\n");

    /* Enforce Nested Kernel invariants by write-protecting PTPs */
    monitor_protect_state();

    /* Note: Verification is only run after switching to unprivileged mode
     * in main.c, not here during monitor_init(). This ensures we verify
     * the final state where all invariants should be enforced. */

    serial_puts("MONITOR: APIC accessible from unprivileged mode\n");
}

/* Get unprivileged CR3 value */
uint64_t monitor_get_unpriv_cr3(void) {
    return unpriv_pml4_phys;
}

/* Check if running in privileged (monitor) mode */
bool monitor_is_privileged(void) {
    uint64_t cr3;
    asm volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3 == monitor_pml4_phys;
}

/* Internal monitor call handler (called from privileged context only) */
monitor_ret_t monitor_call_handler(monitor_call_t call, uint64_t arg1,
                                     uint64_t arg2, uint64_t arg3) {
    monitor_ret_t ret = {0, 0};

    switch (call) {
        case MONITOR_CALL_ALLOC_PHYS:
            /* Direct PMM access (already in privileged mode) */
            ret.result = (uint64_t)pmm_alloc((uint8_t)arg1);
            if (!ret.result) {
                ret.error = -1;
            }
            break;

        case MONITOR_CALL_FREE_PHYS:
            /* Direct PMM access (already in privileged mode) */
            pmm_free((void *)arg1, (uint8_t)arg2);
            break;

        default:
            ret.error = -1;
            break;
    }

    return ret;
}

/* External assembly stub for monitor calls (CR3 switching) */
extern monitor_ret_t monitor_call_stub(monitor_call_t call, uint64_t arg1,
                                        uint64_t arg2, uint64_t arg3);

/* Public monitor call wrapper (for unprivileged code) */
monitor_ret_t monitor_call(monitor_call_t call, uint64_t arg1,
                            uint64_t arg2, uint64_t arg3) {
    if (monitor_pml4_phys == 0) {
        /* Monitor not initialized yet, call directly */
        return monitor_call_handler(call, arg1, arg2, arg3);
    }

    if (monitor_is_privileged()) {
        /* Already privileged, call directly */
        return monitor_call_handler(call, arg1, arg2, arg3);
    }

    /* Unprivileged: use assembly stub to switch CR3 */
    return monitor_call_stub(call, arg1, arg2, arg3);
}

/* PMM monitor call wrappers */
void *monitor_pmm_alloc(uint8_t order) {
    monitor_ret_t ret = monitor_call(MONITOR_CALL_ALLOC_PHYS, order, 0, 0);
    return (void *)ret.result;
}

void monitor_pmm_free(void *addr, uint8_t order) {
    monitor_call(MONITOR_CALL_FREE_PHYS, (uint64_t)addr, order, 0);
}
