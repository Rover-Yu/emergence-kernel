/* monitor.c - Monitor core implementation for nested kernel architecture */

#include "monitor.h"
#include "arch/x86_64/paging.h"

/* External functions */
extern void serial_puts(const char *str);
extern void *pmm_alloc(uint8_t order);
extern void pmm_free(void *addr, uint8_t order);
extern void serial_putc(char c);

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

/* Verify Nested Kernel invariants are correctly configured */
static void monitor_verify_invariants(void) {
    serial_puts("\n=== Nested Kernel Invariant Verification ===\n");

    /* Get physical address of unpriv_pd to find PD entry */
    uint64_t unpriv_pd_phys = virt_to_phys(unpriv_pd);
    int pd_index = monitor_find_pd_entry(unpriv_pd_phys);

    /* Check the PD entry in both views */
    uint64_t unpriv_entry = unpriv_pd[pd_index];
    uint64_t monitor_entry = monitor_pd[pd_index];

    /* Verify writable bit (Invariant 1: protected data is read-only) */
    bool unpriv_writable = (unpriv_entry & X86_PTE_WRITABLE);
    bool monitor_writable = (monitor_entry & X86_PTE_WRITABLE);

    serial_puts("VERIFY: Invariant 1 - PTPs read-only in outer kernel:\n");
    serial_puts("VERIFY:   unpriv_pd writable bit: ");
    serial_putc(unpriv_writable ? '1' : '0');
    serial_puts(" (should be 0)\n");

    serial_puts("VERIFY: Invariant 5 - PTPs writable in nested kernel:\n");
    serial_puts("VERIFY:   monitor_pd writable bit: ");
    serial_putc(monitor_writable ? '1' : '0');
    serial_puts(" (should be 1)\n");

    if (!unpriv_writable && monitor_writable) {
        serial_puts("VERIFY: PASS - Nested Kernel invariants enforced\n");
    } else {
        serial_puts("VERIFY: FAIL - Invariants violated!\n");
    }

    serial_puts("=== Verification Complete ===\n\n");
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

#if CONFIG_WRITE_PROTECTION_VERIFY
    monitor_verify_invariants();
#endif

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
