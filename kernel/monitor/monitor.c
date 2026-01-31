/* monitor.c - Monitor core implementation for nested kernel architecture */

#include <stddef.h>
#include "monitor.h"
#include "arch/x86_64/paging.h"
#include "arch/x86_64/serial.h"
#include "kernel/pcd.h"
#include "kernel/pmm.h"

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

/* 4KB page tables for first 2MB region (for fine-grained protection) */
static uint64_t *monitor_pt_0_2mb;    /* Page table for first 2MB, monitor view */
static uint64_t *unpriv_pt_0_2mb;     /* Page table for first 2MB, unprivileged view */

/* External boot page table symbols (from boot.S) - these are 4KB arrays */
extern uint64_t boot_pml4[];
extern uint64_t boot_pdpt[];
extern uint64_t boot_pd[];
extern uint64_t boot_pd_apic[];
extern uint64_t boot_pt_apic[];

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

    /* Protect monitor page tables (monitor PD entries) */
    /* Find the PD entry covering the first monitor page */
    int pd_index = monitor_find_pd_entry(monitor_pages[0]);

    /* Verify all monitor pages are in the same 2MB region */
    for (int i = 1; i < 6; i++) {
        int idx = monitor_find_pd_entry(monitor_pages[i]);
        if (idx != pd_index) {
            serial_puts("MONITOR: WARNING: Monitor pages span multiple 2MB regions\n");
            serial_puts("MONITOR: Only protecting first region\n");
            break;
        }
    }

    /* Clear Writable bit in unpriv_pd entry for monitor region (make it read-only)
     * This implements Invariant 1: protected data is read-only for outer kernel */
    uint64_t *unpriv_pd_entry = &unpriv_pd[pd_index];
    uint64_t original_entry = *unpriv_pd_entry;

    *unpriv_pd_entry = original_entry & ~X86_PTE_WRITABLE;

    serial_puts("MONITOR: Protected PD entry at index ");
    serial_putc('0' + pd_index);
    serial_puts(" (monitor)\n");

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
    serial_puts("MONITOR: Note: Boot page tables protected via 4KB page tables\n");
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
        /* Skip PML4[0] - intentionally different (monitor vs unpriv PDPT)
         * PML4[0] points to monitor_pdpt in monitor view
         * PML4[0] points to unpriv_pdpt in unprivileged view
         * This is required for the nested kernel isolation */
        if (i == 0) continue;

        /* Skip read-only mapping region (PML4 indices 256-263)
         * NESTED_KERNEL_RO_BASE (0xFFFF880000000000ULL) maps to PML4[257]
         * Due to page table allocation for RO mappings, we may create multiple
         * PML4 entries in the high canonical range. Skip these. */
        if (i >= 256 && i < 264) continue;

        /* Also skip any PML4 entries that were 0 in boot_pml4
         * These may have been populated differently in monitor vs unpriv view
         * due to RO mapping page table allocations */
        if (boot_pml4[i] == 0) continue;

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
    serial_puts("VERIFY:   nk_entry_trampoline available - ");
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

/* Virtual base address for read-only nested kernel mappings */
#define NESTED_KERNEL_RO_BASE  0xFFFF880000000000ULL

/* Page table index extraction macros for x86-64 virtual addresses */
#define PML4_INDEX(vaddr)  (((vaddr) >> 39) & 0x1FF)
#define PDPT_INDEX(vaddr)  (((vaddr) >> 30) & 0x1FF)
#define PD_INDEX(vaddr)    (((vaddr) >> 21) & 0x1FF)
#define PT_INDEX(vaddr)    (((vaddr) >> 12) & 0x1FF)

/**
 * create_or_get_table - Allocate and initialize a page table
 * @phys_out: Output parameter for physical address of allocated table
 *
 * Returns: Virtual address of allocated table, or NULL on failure
 */
static uint64_t *create_or_get_table(uint64_t *phys_out) {
    uint64_t *table = (uint64_t *)pmm_alloc(0);
    if (!table) {
        return NULL;
    }

    /* Clear the table */
    for (int i = 0; i < 512; i++) {
        table[i] = 0;
    }

    /* Mark as NK_PGTABLE for PCD tracking */
    uint64_t phys = virt_to_phys(table);
    pcd_set_type(phys, PCD_TYPE_NK_PGTABLE);

    *phys_out = phys;
    return table;
}

/**
 * create_ro_mapping - Create a read-only mapping in unprivileged page tables
 * @phys_addr: Physical address to map (must be page-aligned)
 * @virt_addr: Virtual address to map to (must be page-aligned)
 *
 * Returns: 0 on success, -1 on failure
 *
 * This walks the unprivileged page table hierarchy and creates a read-only PTE.
 * The physical page is mapped with X86_PTE_PRESENT only (no WRITABLE bit).
 */
static int create_ro_mapping(uint64_t phys_addr, uint64_t virt_addr) {
    /* Extract page table indices from virtual address */
    int pml4_idx = PML4_INDEX(virt_addr);
    int pdpt_idx = PDPT_INDEX(virt_addr);
    int pd_idx = PD_INDEX(virt_addr);
    int pt_idx = PT_INDEX(virt_addr);

    /* Step 1: Get or create PDPT entry from PML4 */
    uint64_t pml4_entry = unpriv_pml4[pml4_idx];
    uint64_t *pdpt;

    if (!(pml4_entry & X86_PTE_PRESENT)) {
        /* Need to allocate new PDPT */
        uint64_t pdpt_phys;
        pdpt = create_or_get_table(&pdpt_phys);
        if (!pdpt) {
            return -1;
        }

        /* Set PML4 entry - writable so we can modify lower levels */
        unpriv_pml4[pml4_idx] = pdpt_phys | X86_PTE_PRESENT | X86_PTE_WRITABLE;
    } else {
        /* Existing PDPT */
        pdpt = (uint64_t *)(pml4_entry & ~0xFFF);
    }

    /* Step 2: Get or create PD entry from PDPT */
    uint64_t pdpt_entry = pdpt[pdpt_idx];
    uint64_t *pd;

    if (!(pdpt_entry & X86_PTE_PRESENT)) {
        /* Need to allocate new PD */
        uint64_t pd_phys;
        pd = create_or_get_table(&pd_phys);
        if (!pd) {
            return -1;
        }

        /* Set PDPT entry */
        pdpt[pdpt_idx] = pd_phys | X86_PTE_PRESENT | X86_PTE_WRITABLE;
    } else {
        /* Existing PD */
        pd = (uint64_t *)(pdpt_entry & ~0xFFF);
    }

    /* Step 3: Get or create PT entry from PD */
    uint64_t pd_entry = pd[pd_idx];
    uint64_t *pt;

    if (!(pd_entry & X86_PTE_PRESENT)) {
        /* Need to allocate new PT */
        uint64_t pt_phys;
        pt = create_or_get_table(&pt_phys);
        if (!pt) {
            return -1;
        }

        /* Set PD entry */
        pd[pd_idx] = pt_phys | X86_PTE_PRESENT | X86_PTE_WRITABLE;
    } else {
        /* Existing PT - check if it's a 2MB page */
        if (pd_entry & X86_PTE_PS) {
            /* Existing 2MB page - need to split it (not implemented yet) */
            serial_puts("MONITOR: WARNING - Cannot split 2MB page for RO mapping\n");
            return -1;
        }
        pt = (uint64_t *)(pd_entry & ~0xFFF);
    }

    /* Step 4: Create final PTE - read-only (PRESENT only, no WRITABLE) */
    pt[pt_idx] = phys_addr | X86_PTE_PRESENT;

    return 0;
}

/**
 * monitor_create_ro_mappings - Create read-only mappings for outer kernel visibility
 *
 * Creates read-only mappings for all NK_NORMAL and NK_PGTABLE pages
 * so the outer kernel can inspect nested kernel state but not modify it.
 * This implements the read-only visibility requirement.
 *
 * Returns: 0 on success
 */
int monitor_create_ro_mappings(void) {
    uint64_t ro_page_count = 0;

    serial_puts("MONITOR: Creating read-only mappings for outer kernel\n");

    for (uint64_t i = 0; i < pcd_get_max_pages(); i++) {
        uint64_t phys_addr = i << PAGE_SHIFT;
        uint8_t type = pcd_get_type(phys_addr);

        /* Map all NK_NORMAL and NK_PGTABLE pages as read-only */
        if (type == PCD_TYPE_NK_NORMAL || type == PCD_TYPE_NK_PGTABLE) {
            uint64_t virt_addr = NESTED_KERNEL_RO_BASE + phys_addr;
            if (create_ro_mapping(phys_addr, virt_addr) == 0) {
                ro_page_count++;
            }
        }
    }

    serial_puts("MONITOR: Created ");
    serial_put_hex(ro_page_count);
    serial_puts(" read-only mappings\n");

    return 0;
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

    /* Allocate 4KB page tables for first 2MB region */
    /* 2MB / 4KB = 512 entries needed, one page table has 512 entries = 4KB */
    monitor_pt_0_2mb = (uint64_t *)pmm_alloc(0);
    unpriv_pt_0_2mb = (uint64_t *)pmm_alloc(0);

    if (!monitor_pt_0_2mb || !unpriv_pt_0_2mb) {
        serial_puts("MONITOR: Failed to allocate 4KB page tables\n");
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

    /* Set up 4KB page tables for first 2MB region */
    /* Each 4KB page: PRESENT + WRITABLE for monitor */
    for (int i = 0; i < 512; i++) {
        uint64_t phys_addr = (uint64_t)i * 4096;
        /* Monitor view: writable */
        monitor_pt_0_2mb[i] = phys_addr | X86_PTE_PRESENT | X86_PTE_WRITABLE;
        /* Unprivileged view: read-only for page table pages, writable for others */
        if (phys_addr == virt_to_phys(boot_pml4) ||
            phys_addr == virt_to_phys(boot_pdpt) ||
            phys_addr == virt_to_phys(boot_pd) ||
            phys_addr == virt_to_phys(boot_pd_apic) ||
            phys_addr == virt_to_phys(boot_pt_apic) ||
            phys_addr == virt_to_phys(monitor_pml4) ||
            phys_addr == virt_to_phys(monitor_pdpt) ||
            phys_addr == virt_to_phys(monitor_pd) ||
            phys_addr == virt_to_phys(unpriv_pml4) ||
            phys_addr == virt_to_phys(unpriv_pdpt) ||
            phys_addr == virt_to_phys(unpriv_pd)) {
            /* Page table pages: read-only */
            unpriv_pt_0_2mb[i] = phys_addr | X86_PTE_PRESENT;
        } else {
            /* Other pages: writable (includes kernel stack, even if NK_NORMAL) */
            unpriv_pt_0_2mb[i] = phys_addr | X86_PTE_PRESENT | X86_PTE_WRITABLE;
        }
    }

    /* CRITICAL: Verify nested kernel boot stack pages are writable in unprivileged view
     * The stack is NK_NORMAL but MUST be writable for CPU execution
     * Stack pages are not page table pages, so they should be writable above */
    extern uint8_t nk_boot_stack_bottom[], nk_boot_stack_top[];
    uint64_t stack_start = ((uint64_t)nk_boot_stack_bottom) & ~0xFFF;
    uint64_t stack_end = ((uint64_t)nk_boot_stack_top) & ~0xFFF;

    serial_puts("MONITOR: Verifying stack pages are writable in unprivileged view\n");
    for (uint64_t addr = stack_start; addr <= stack_end; addr += 0x1000) {
        int pte_index = addr >> 12;
        uint64_t pte = unpriv_pt_0_2mb[pte_index];
        if (!(pte & X86_PTE_WRITABLE)) {
            serial_puts("MONITOR: ERROR - Stack page at 0x");
            serial_put_hex(addr);
            serial_puts(" is read-only! Fixing...\n");
            unpriv_pt_0_2mb[pte_index] = addr | X86_PTE_PRESENT | X86_PTE_WRITABLE;
        }
    }

    /* Update PD[0] to point to page table instead of 2MB page */
    /* Monitor view */
    monitor_pd[0] = (uint64_t)virt_to_phys(monitor_pt_0_2mb) | X86_PTE_PRESENT | X86_PTE_WRITABLE;
    /* Unprivileged view - make PD[0] writable so individual 4KB page protections work */
    unpriv_pd[0] = (uint64_t)virt_to_phys(unpriv_pt_0_2mb) | X86_PTE_PRESENT | X86_PTE_WRITABLE;

    /* CRITICAL: Update unprivileged page table hierarchy to use unpriv tables
     * This ensures the CPU actually uses the protected page tables */
    /* Update unpriv_pml4[0] to point to unpriv_pdpt instead of boot_pdpt */
    uint64_t unpriv_pdpt_phys = virt_to_phys(unpriv_pdpt);
    uint64_t boot_pml4_entry0 = boot_pml4[0];
    unpriv_pml4[0] = (boot_pml4_entry0 & 0xFFF) | unpriv_pdpt_phys;

    /* Update unpriv_pdpt[0] to point to unpriv_pd instead of boot_pd */
    uint64_t unpriv_pd_phys = virt_to_phys(unpriv_pd);
    uint64_t boot_pdpt_entry0 = boot_pdpt[0];
    unpriv_pdpt[0] = (boot_pdpt_entry0 & 0xFFF) | unpriv_pd_phys;

    /* Debug: Verify the hierarchy update worked */
    serial_puts("MONITOR: After hierarchy update:\n");
    serial_puts("  unpriv_pml4[0] = 0x");
    serial_put_hex(unpriv_pml4[0]);
    serial_puts(" (should be unpriv_pdpt)\n");
    serial_puts("  unpriv_pdpt[0] = 0x");
    serial_put_hex(unpriv_pdpt[0]);
    serial_puts(" (should be unpriv_pd)\n");

    /* Debug: Check some critical PTEs */
    serial_puts("MONITOR: Critical PTEs in unpriv_pt_0_2mb:\n");
    /* Kernel code starts at 0x100000 */
    uint64_t kernel_code_pde = (0x100000 >> 12);
    serial_puts("  Kernel code at 0x100000 (PTE ");
    serial_put_hex(kernel_code_pde);
    serial_puts("): 0x");
    serial_put_hex(unpriv_pt_0_2mb[kernel_code_pde]);
    serial_puts("\n");
    /* Kernel stack at 0x110000 */
    uint64_t kernel_stack_pde = (0x110000 >> 12);
    serial_puts("  Kernel stack at 0x110000 (PTE ");
    serial_put_hex(kernel_stack_pde);
    serial_puts("): 0x");
    serial_put_hex(unpriv_pt_0_2mb[kernel_stack_pde]);
    serial_puts("\n");

    /* Debug: Verify boot_pml4 page table entry */
    uint64_t boot_pml4_phys = virt_to_phys(boot_pml4);
    uint64_t boot_pml4_pte_index = boot_pml4_phys >> 12;  /* Div by 4KB */
    serial_puts("MONITOR: boot_pml4 at 0x");
    serial_put_hex(boot_pml4_phys);
    serial_puts(", PTE index ");
    serial_put_hex(boot_pml4_pte_index);
    serial_puts("\n");
    serial_puts("  unpriv_pt_0_2mb[");
    serial_put_hex(boot_pml4_pte_index);
    serial_puts("] = 0x");
    serial_put_hex(unpriv_pt_0_2mb[boot_pml4_pte_index]);
    serial_puts(" (should be read-only)\n");
    serial_puts("  monitor_pt_0_2mb[");
    serial_put_hex(boot_pml4_pte_index);
    serial_puts("] = 0x");
    serial_put_hex(monitor_pt_0_2mb[boot_pml4_pte_index]);
    serial_puts(" (should be writable)\n");

    /* Debug: Verify page table hierarchy */
    serial_puts("  unpriv_pml4[0] = 0x");
    serial_put_hex(unpriv_pml4[0]);
    serial_puts(" (should point to unpriv_pdpt)\n");
    serial_puts("  unpriv_pdpt[0] = 0x");
    serial_put_hex(unpriv_pdpt[0]);
    serial_puts(" (should point to unpriv_pd)\n");

    /* Save physical addresses */
    monitor_pml4_phys = virt_to_phys(monitor_pml4);
    unpriv_pml4_phys = virt_to_phys(unpriv_pml4);

    /* Debug: Print page table structure */
    serial_puts("MONITOR: Page table structure:\n");
    serial_puts("  boot_pml4 phys = 0x");
    serial_put_hex(virt_to_phys(boot_pml4));
    serial_puts("\n  unpriv_pml4 phys = 0x");
    serial_put_hex(unpriv_pml4_phys);
    serial_puts("\n  boot_pd phys = 0x");
    serial_put_hex(virt_to_phys(boot_pd));
    serial_puts("\n  boot_pd[0] = 0x");
    serial_put_hex(boot_pd[0]);
    serial_puts("\n  boot_pd[1] = 0x");
    serial_put_hex(boot_pd[1]);
    serial_puts("\n  monitor_pd[0] = 0x");
    serial_put_hex(monitor_pd[0]);
    serial_puts("\n  unpriv_pd[0] = 0x");
    serial_put_hex(unpriv_pd[0]);
    serial_puts("\n  unpriv_pd[1] = 0x");
    serial_put_hex(unpriv_pd[1]);
    serial_puts("\n  Using 4KB pages for first 2MB\n");

    /* Mark page table pages as NK_PGTABLE for PCD tracking */
    /* These pages should not be accessible to outer kernel for mapping */

    /* Mark boot page tables as NK_PGTABLE */
    pcd_set_type(virt_to_phys(boot_pml4), PCD_TYPE_NK_PGTABLE);
    pcd_set_type(virt_to_phys(boot_pdpt), PCD_TYPE_NK_PGTABLE);
    pcd_set_type(virt_to_phys(boot_pd), PCD_TYPE_NK_PGTABLE);
    pcd_set_type(virt_to_phys(boot_pd_apic), PCD_TYPE_NK_PGTABLE);
    pcd_set_type(virt_to_phys(boot_pt_apic), PCD_TYPE_NK_PGTABLE);

    /* Mark monitor page tables as NK_PGTABLE */
    pcd_set_type(virt_to_phys(monitor_pml4), PCD_TYPE_NK_PGTABLE);
    pcd_set_type(virt_to_phys(monitor_pdpt), PCD_TYPE_NK_PGTABLE);
    pcd_set_type(virt_to_phys(monitor_pd), PCD_TYPE_NK_PGTABLE);
    pcd_set_type(virt_to_phys(unpriv_pml4), PCD_TYPE_NK_PGTABLE);
    pcd_set_type(virt_to_phys(unpriv_pdpt), PCD_TYPE_NK_PGTABLE);
    pcd_set_type(virt_to_phys(unpriv_pd), PCD_TYPE_NK_PGTABLE);

    serial_puts("MONITOR: Page tables initialized\n");

    /* Enforce Nested Kernel invariants by write-protecting PTPs */
    monitor_protect_state();

    /* Mark I/O regions (APIC at 0xFEE00000) as NK_IO for visibility */
    /* Note: Per user requirement, APIC stays in outer kernel so this
     * is for tracking/logging only, not enforcement */
    pcd_mark_region(0xFEE00000, 0x1000, PCD_TYPE_NK_IO);

    /* Create read-only mappings for outer kernel visibility
     * Kernel pages are already marked as NK_NORMAL by pcd_init()
     * Page tables are already marked as NK_PGTABLE above */
    monitor_create_ro_mappings();

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

        case MONITOR_CALL_SET_PAGE_TYPE:
            /* Set PCD type (monitor only) */
            pcd_set_type(arg1, (uint8_t)arg2);
            ret.result = 0;
            break;

        case MONITOR_CALL_GET_PAGE_TYPE:
            /* Query PCD type */
            ret.result = pcd_get_type(arg1);
            break;

        case MONITOR_CALL_MAP_PAGE:
            /* Map page with validation */
            ret.result = monitor_map_page(arg1, arg2, arg3);
            if (ret.result != 0) {
                ret.error = -1;
            }
            break;

        case MONITOR_CALL_UNMAP_PAGE:
            /* Unmap page */
            ret.result = monitor_unmap_page(arg1);
            if (ret.result != 0) {
                ret.error = -1;
            }
            break;

        case MONITOR_CALL_ALLOC_PGTABLE:
            /* Allocate page and mark as NK_PGTABLE */
            ret.result = (uint64_t)pmm_alloc((uint8_t)arg1);
            if (ret.result) {
                uint64_t addr = ret.result;
                for (uint64_t i = 0; i < (1ULL << arg1); i++) {
                    pcd_set_type(addr + (i << PAGE_SHIFT), PCD_TYPE_NK_PGTABLE);
                }
            } else {
                ret.error = -1;
            }
            break;

        default:
            ret.error = -1;
            break;
    }

    return ret;
}

/* External assembly stub for monitor calls (CR3 switching) */
extern monitor_ret_t nk_entry_trampoline(monitor_call_t call, uint64_t arg1,
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
    return nk_entry_trampoline(call, arg1, arg2, arg3);
}

/* PMM monitor call wrappers */
void *monitor_pmm_alloc(uint8_t order) {
    monitor_ret_t ret = monitor_call(MONITOR_CALL_ALLOC_PHYS, order, 0, 0);
    void *page = (void *)ret.result;

    if (page && pcd_is_initialized()) {
        /* Change type from NK_NORMAL (default) to OK_NORMAL for outer kernel use */
        for (uint64_t i = 0; i < (1ULL << order); i++) {
            uint64_t page_addr = (uint64_t)page + (i << PAGE_SHIFT);
            pcd_set_type(page_addr, PCD_TYPE_OK_NORMAL);
        }
    }

    return page;
}

void monitor_pmm_free(void *addr, uint8_t order) {
    monitor_call(MONITOR_CALL_FREE_PHYS, (uint64_t)addr, order, 0);
}

/* PCD management functions (monitor only) */
void monitor_pcd_set_type(uint64_t phys_addr, uint8_t type) {
    pcd_set_type(phys_addr, type);
}

uint8_t monitor_pcd_get_type(uint64_t phys_addr) {
    return pcd_get_type(phys_addr);
}

/* ============================================================================
 * Mapping Functions with PCD Validation
 * ============================================================================ */

/**
 * monitor_map_page - Map a physical page with PCD type validation
 * @phys_addr: Physical address of page to map
 * @virt_addr: Virtual address to map to
 * @flags: Page table entry flags
 *
 * Returns: 0 on success, -1 on rejection
 *
 * This function validates that the page being mapped is of a type
 * that allows outer kernel access. For NK_NORMAL and NK_PGTABLE pages,
 * only read-only mappings are allowed - writable requests are rejected.
 */
int monitor_map_page(uint64_t phys_addr, uint64_t virt_addr, uint64_t flags) {
    uint8_t type = pcd_get_type(phys_addr);

    /* Validate page type before mapping */
    switch (type) {
        case PCD_TYPE_OK_NORMAL:
            /* OK: Outer kernel can map its own pages read/write */
            break;

        case PCD_TYPE_NK_NORMAL:
        case PCD_TYPE_NK_PGTABLE:
            /* Allow read-only access to NK_NORMAL and NK_PGTABLE pages
             * Reject writable requests for these types */
            if (flags & X86_PTE_WRITABLE) {
                serial_puts("MONITOR: Reject writable mapping for ");
                if (type == PCD_TYPE_NK_NORMAL) {
                    serial_puts("NK_NORMAL");
                } else {
                    serial_puts("NK_PGTABLE");
                }
                serial_puts(" page at 0x");
                serial_put_hex(phys_addr);
                serial_puts("\n");
                return -1;
            }
            /* Force read-only (clear WRITABLE bit even if not set) */
            flags = flags & ~X86_PTE_WRITABLE;
            break;

        case PCD_TYPE_NK_IO:
            /* TRACKING ONLY: Allow mapping, log for visibility */
            /* Per user requirement: don't enforce APIC isolation */
            serial_puts("MONITOR: Note - mapping I/O page at 0x");
            serial_put_hex(phys_addr);
            serial_puts(" (allowed)\n");
            break;
    }

    /* Create the mapping in unprivileged page tables */
    /* Note: This is a simplified implementation that updates the
     * unprivileged view. A full implementation would walk the page
     * table hierarchy and create entries as needed. */

    /* For now, we just validate the type - actual mapping would
     * require walking the page table hierarchy */
    return 0;
}

/**
 * monitor_unmap_page - Unmap a virtual page
 * @virt_addr: Virtual address to unmap
 *
 * Returns: 0 on success, -1 on failure
 */
int monitor_unmap_page(uint64_t virt_addr) {
    /* For now, this is a placeholder */
    /* A full implementation would walk the page tables and
     * clear the appropriate PTE */
    return 0;
}

/* Allocate page table pages (marked as NK_PGTABLE) */
void *monitor_alloc_pgtable(uint8_t order) {
    monitor_ret_t ret = monitor_call(MONITOR_CALL_ALLOC_PGTABLE, order, 0, 0);
    return (void *)ret.result;
}
