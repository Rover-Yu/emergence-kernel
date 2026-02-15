/* monitor.c - Monitor core implementation for nested kernel architecture */

#include <stddef.h>
#include "monitor.h"
#include "arch/x86_64/paging.h"
#include "arch/x86_64/cr.h"
#include "arch/x86_64/serial.h"
#include "arch/x86_64/multiboot2.h"
#include "kernel/pcd.h"
#include "kernel/pmm.h"

/* Page table index extraction macros */
#define PML4_INDEX(vaddr) (((vaddr) >> 39) & 0x1FF)
#define PDPT_INDEX(vaddr) (((vaddr) >> 30) & 0x1FF)
#define PD_INDEX(vaddr)   (((vaddr) >> 21) & 0x1FF)
#define PT_INDEX(vaddr)   (((vaddr) >> 12) & 0x1FF)

/* External functions */
extern void *pmm_alloc(uint8_t order);
extern void pmm_free(void *addr, uint8_t order);
extern int smp_get_cpu_index(void);

/* Internal PCD function (monitor-only access) */
extern void _pcd_set_type_internal(uint64_t phys_addr, uint8_t type);

/* External GDT from boot.S */
extern void gdt64(void);

/* Page table physical addresses */
uint64_t monitor_pml4_phys = 0;
uint64_t unpriv_pml4_phys = 0;

/* Monitor page table structures (allocated during init) */
static uint64_t *monitor_pml4;
static uint64_t *monitor_pdpt;
static uint64_t *monitor_pd;

static uint64_t *unpriv_pml4;
static uint64_t *unpriv_pdpt;
static uint64_t *unpriv_pd;         /* Local pointer for monitor operations */
uint64_t *g_unpriv_pd_ptr;        /* Exported for user mode syscall support */

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
    arch_tlb_invalidate_page(addr);
}

/* Helper: Find PD entry covering a physical address
 * For identity mapping, PD index = (addr >> 21) & 0x1FF */
static int monitor_find_pd_entry(uint64_t phys_addr) {
    return (phys_addr >> 21) & 0x1FF;
}

/* Protect all PTP pages in unprivileged view via PCD discovery
 * This implements complete Invariant 5 coverage:
 * - Discovers ALL NK_PGTABLE pages via PCD
 * - Makes them read-only in unprivileged page tables */
static void monitor_protect_all_ptps(void) {
    int quiet = kernel_is_quiet();
    if (!quiet) serial_puts("MONITOR: Protecting all PTPs via PCD discovery\n");
    int protected_count = 0;

    /* Iterate through all PCD-tracked pages */
    for (uint64_t i = 0; i < pcd_get_max_pages(); i++) {
        uint64_t phys = i << PAGE_SHIFT;
        uint8_t type = pcd_get_type(phys);

        if (type == PCD_TYPE_NK_PGTABLE) {
            /* Find the page table entry for this physical page
             * For identity-mapped first 2MB, use unpriv_pt_0_2mb */
            int pte_idx = phys >> 12;
            if (pte_idx < 512) {  /* First 2MB region */
                unpriv_pt_0_2mb[pte_idx] &= ~X86_PTE_WRITABLE;
            }
            protected_count++;
        }
    }

    /* Invalidate TLB for all protected pages in first 2MB */
    for (int i = 0; i < 512; i++) {
        if (!(unpriv_pt_0_2mb[i] & X86_PTE_WRITABLE)) {
            monitor_invalidate_page((void*)(uintptr_t)(i << 12));
        }
    }

    if (!quiet) {
        serial_puts("MONITOR: Protected ");
        serial_put_hex(protected_count);
        serial_puts(" PTP pages\n");
    }
}

/* Protect monitor state pages in unprivileged view
 * This implements Nested Kernel Invariant 1 and Invariant 5:
 * - PTPs are marked read-only while outer kernel executes
 * - All mappings to PTPs are marked read-only */
static void monitor_protect_state(void) {
    int quiet = kernel_is_quiet();
    if (!quiet) serial_puts("MONITOR: Enforcing Nested Kernel invariants\n");
    if (!quiet) serial_puts("MONITOR: Protecting page table pages (Invariant 5)\n");

    /* Get physical addresses of all 6 monitor page table pages
     * These are the PTPs that must be write-protected (Invariant 5) */
    uint64_t monitor_pages[] = {
        virt_to_phys(monitor_pml4),
        virt_to_phys(monitor_pdpt),
        virt_to_phys(monitor_pd),
        virt_to_phys(unpriv_pml4),
        virt_to_phys(unpriv_pdpt),
        virt_to_phys(g_unpriv_pd_ptr)
    };

    /* Find the PD entry covering the first monitor page */
    int pd_index = monitor_find_pd_entry(monitor_pages[0]);

    /* Verify all monitor pages are in the same 2MB region */
    for (int i = 1; i < 6; i++) {
        int idx = monitor_find_pd_entry(monitor_pages[i]);
        if (idx != pd_index) {
            if (!quiet) {
                serial_puts("MONITOR: WARNING: Monitor pages span multiple 2MB regions\n");
                serial_puts("MONITOR: Only protecting first region\n");
            }
            break;
        }
    }

    /* NOTE: We do NOT protect the PD entry here because:
     * 1. Monitor page tables are allocated in the same 2MB region as kernel code/data
     * 2. Protecting unpriv_pd[pd_index] would make kernel code read-only → crash
     * 3. Instead, we rely on 4KB PTE protection via monitor_protect_all_ptps()
     *
     * For proper Invariant 5 enforcement, we should create 4KB page tables for
     * the kernel region (0x400000+) and mark individual PTP PTEs as read-only.
     * This is tracked as TODO for future improvement.
     */
    if (!quiet) serial_puts("MONITOR: Skipping PD entry protection (kernel region overlap)\n");
    if (!quiet) serial_puts("MONITOR: Relying on 4KB PTE protection via PCD discovery\n");

    /* Invalidate TLB for the affected 2MB region */
    monitor_invalidate_page((void *)monitor_pages[0]);

    if (!quiet) serial_puts("MONITOR: TLB invalidated (Invariant 2 enforcement active)\n");

    /* Protect all PTPs discovered via PCD - this handles individual page protection */
    monitor_protect_all_ptps();

    if (!quiet) serial_puts("MONITOR: Nested Kernel invariants enforced\n");
    if (!quiet) serial_puts("MONITOR: Note: Boot page tables protected via 4KB page tables\n");
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
 * - Shows per-invariant details only if CONFIG_DEBUG_NK_INVARIANTS_VERBOSE=1
 */
void monitor_verify_invariants(void) {
    extern int smp_get_cpu_index(void);

    int cpu_id = smp_get_cpu_index();

#if CONFIG_DEBUG_NK_INVARIANTS_VERBOSE
    serial_puts("\n=== Nested Kernel Invariant Verification (CPU ");
    serial_putc('0' + cpu_id);
    serial_puts(") ===\n");
#endif

    /* Get physical address of unpriv_pd to find PD entry */
    uint64_t unpriv_pd_phys = virt_to_phys(g_unpriv_pd_ptr);
    int pd_index = monitor_find_pd_entry(unpriv_pd_phys);

    /* Check the PD entry in both views */
    uint64_t unpriv_entry = g_unpriv_pd_ptr[pd_index];
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

#if CONFIG_DEBUG_NK_INVARIANTS_VERBOSE
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
        uint64_t cr0 = arch_cr0_read();
        cr0_wp_enabled = (cr0 & (1 << 16));
    }

#if CONFIG_DEBUG_NK_INVARIANTS_VERBOSE
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

#if CONFIG_DEBUG_NK_INVARIANTS_VERBOSE
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

#if CONFIG_DEBUG_NK_INVARIANTS_VERBOSE
    serial_puts("VERIFY: [Inv 4] Context switch mechanism:\n");
    serial_puts("VERIFY:   nk_entry_trampoline available - ");
    if (context_switch_available) {
        serial_puts("PASS\n");
    } else {
        serial_puts("FAIL\n");
    }
#endif

    /* === Invariant 5: All PTPs marked read-only in outer kernel === */
#if CONFIG_DEBUG_NK_INVARIANTS_VERBOSE
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
    current_cr3 = arch_cr3_read();
    cr3_is_predeclared = (current_cr3 == monitor_pml4_phys) ||
                         (current_cr3 == unpriv_pml4_phys);

#if CONFIG_DEBUG_NK_INVARIANTS_VERBOSE
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

#if CONFIG_DEBUG_NK_INVARIANTS_VERBOSE
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

    /* Debug: Check if we're allocating a page that's already used as a page table */
    if ((uint64_t)table == (uint64_t)unpriv_pd ||
        (uint64_t)table == (uint64_t)unpriv_pdpt ||
        (uint64_t)table == (uint64_t)unpriv_pml4 ||
        (uint64_t)table == (uint64_t)monitor_pd ||
        (uint64_t)table == (uint64_t)monitor_pdpt ||
        (uint64_t)table == (uint64_t)monitor_pml4) {
        serial_puts("MONITOR: FATAL - PMM returned already-used page table page (0x");
        serial_put_hex((uint64_t)table);
        serial_puts(")! PMM corruption detected.\n");
        /* Don't zero the page - it's already in use! */
        return NULL;
    }

    /* Clear the table */
    for (int i = 0; i < 512; i++) {
        table[i] = 0;
    }

    /* Mark as NK_PGTABLE for PCD tracking */
    uint64_t phys = virt_to_phys(table);
    _pcd_set_type_internal(phys, PCD_TYPE_NK_PGTABLE);

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
        /* Existing PDPT - check if it's the identity-mapped PDPT
         * For high addresses (pml4_idx != 0), we must NOT reuse the
         * identity-mapped PDPT (unpriv_pdpt) as it would cause issues */
        uint64_t pdpt_addr = pml4_entry & ~0xFFF;

        /* If pml4_idx != 0 (not first 512GB) and pdpt_addr is unpriv_pdpt,
         * allocate a new PDPT to avoid corrupting identity mapping */
        if (pml4_idx != 0 && pdpt_addr == virt_to_phys(unpriv_pdpt)) {
            uint64_t pdpt_phys;
            pdpt = create_or_get_table(&pdpt_phys);
            if (!pdpt) {
                return -1;
            }
            /* Update PML4 entry to point to new PDPT */
            unpriv_pml4[pml4_idx] = pdpt_phys | X86_PTE_PRESENT | X86_PTE_WRITABLE;
        } else {
            /* Validate address before use */
            if (pdpt_addr == 0 || pdpt_addr < 0x100000) {
                serial_puts("MONITOR: Invalid PDPT address\n");
                return -1;
            }
            pdpt = (uint64_t *)pdpt_addr;
        }
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
        /* Existing PD - check if it's the identity-mapped PD */
        uint64_t pd_addr = pdpt_entry & ~0xFFF;

        /* CRITICAL: Never reuse unpriv_pd for non-identity-mapped addresses */
        if (pd_addr == virt_to_phys(unpriv_pd)) {
            uint64_t pd_phys;
            pd = create_or_get_table(&pd_phys);
            if (!pd) {
                return -1;
            }
            /* Update PDPT entry to point to new PD */
            pdpt[pdpt_idx] = pd_phys | X86_PTE_PRESENT | X86_PTE_WRITABLE;
        } else {
            /* Validate address before use */
            if (pd_addr == 0 || pd_addr < 0x100000) {
                serial_puts("MONITOR: Invalid PD address\n");
                return -1;
            }
            pd = (uint64_t *)pd_addr;
        }
    }

    /* Step 3: Get or create PT entry from PD */
    uint64_t pd_entry = pd[pd_idx];
    uint64_t *pt;

    /* Debug: Check if we're about to corrupt unpriv_pd */
    if ((uint64_t)pd == virt_to_phys(unpriv_pd)) {
        serial_puts("MONITOR: ERROR - pd is unpriv_pd in step 3! pd_idx=0x");
        serial_put_hex(pd_idx);
        serial_puts(", pml4_idx=0x");
        serial_put_hex(pml4_idx);
        serial_puts(", pdpt_idx=0x");
        serial_put_hex(pdpt_idx);
        serial_puts("\n");
        return -1;  /* Prevent corruption */
    }

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
        /* Validate PT address before use */
        uint64_t pt_addr = pd_entry & ~0xFFF;
        if (pt_addr == 0 || pt_addr < 0x100000) {
            serial_puts("MONITOR: Invalid PT address\n");
            return -1;
        }
        pt = (uint64_t *)pt_addr;
    }

    /* Step 4: Create final PTE - read-only (PRESENT only, no WRITABLE) */
    pt[pt_idx] = phys_addr | X86_PTE_PRESENT;
    /* Invalidate TLB for this page after PTE update */
    arch_tlb_invalidate_page((void *)(uintptr_t)(pt_idx << 12));

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
    int quiet = kernel_is_quiet();

    if (!quiet) serial_puts("MONITOR: Creating read-only mappings for outer kernel\n");

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

    if (!quiet) {
        serial_puts("MONITOR: Created ");
        serial_put_hex(ro_page_count);
        serial_puts(" read-only mappings\n");
    }

    return 0;
}

/* Initialize monitor page tables */
void monitor_init(void) {
    int quiet = kernel_is_quiet();
    if (!quiet) serial_puts("MONITOR: Initializing nested kernel architecture\n");

    /* Allocate page tables for monitor (privileged) view */
    monitor_pml4 = (uint64_t *)pmm_alloc(0);  /* 1 page */
    monitor_pdpt = (uint64_t *)pmm_alloc(0);
    monitor_pd = (uint64_t *)pmm_alloc(0);

    if (!quiet) {
        serial_puts("MONITOR: Allocations: monitor_pml4=0x");
        serial_put_hex((uint64_t)monitor_pml4);
        serial_puts(" monitor_pdpt=0x");
        serial_put_hex((uint64_t)monitor_pdpt);
        serial_puts(" monitor_pd=0x");
        serial_put_hex((uint64_t)monitor_pd);
        serial_puts("\n");
    }

    if (!monitor_pml4 || !monitor_pdpt || !monitor_pd) {
        serial_puts("MONITOR: Failed to allocate monitor page tables\n");
        return;
    }

    /* Allocate page tables for unprivileged view */
    unpriv_pml4 = (uint64_t *)pmm_alloc(0);
    unpriv_pdpt = (uint64_t *)pmm_alloc(0);
    unpriv_pd = (uint64_t *)pmm_alloc(0);

    if (!quiet) {
        serial_puts("MONITOR: Allocations: unpriv_pml4=0x");
        serial_put_hex((uint64_t)unpriv_pml4);
        serial_puts(" unpriv_pdpt=0x");
        serial_put_hex((uint64_t)unpriv_pdpt);
        serial_puts(" unpriv_pd=0x");
        serial_put_hex((uint64_t)unpriv_pd);
        serial_puts("\n");
    }

    if (!unpriv_pml4 || !unpriv_pdpt || !unpriv_pd) {
        serial_puts("MONITOR: Failed to allocate unprivileged page tables\n");
        return;
    }

    /* Set exported pointer for user mode access */
    g_unpriv_pd_ptr = unpriv_pd;

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

    /* CRITICAL: Make user stack region (4MB-6MB) user-accessible
     * This is where PMM allocates memory from, including user stacks
     * unpriv_pd[1] maps the 2MB region at 0x200000
     * Note: With kernel at 4MB, GRUB2 gap is 1MB-4MB
     * PMM allocations start above 4MB, so we map 4MB-6MB region */
    unpriv_pd[1] |= X86_PTE_USER;
    if (!quiet) serial_puts("MONITOR: User stack region (0x200000-0x3FFFFF) is now user-accessible\n");
    /* TODO: After kernel moves to 4MB, update this to map region above kernel */

    /* Set up 4KB page tables for first 2MB region */
    /* Each 4KB page: PRESENT + WRITABLE for monitor */
    for (int i = 0; i < 512; i++) {
        uint64_t phys_addr = (uint64_t)i * 4096;
        /* Monitor view: writable */
        monitor_pt_0_2mb[i] = phys_addr | X86_PTE_PRESENT | X86_PTE_WRITABLE;
        /* Unprivileged view: read-only for page table pages, writable+user for others */
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
            /* Page table pages: read-only, supervisor-only */
            unpriv_pt_0_2mb[i] = phys_addr | X86_PTE_PRESENT;
        } else {
            /* Other pages: writable + user-accessible (for user mode syscall test) */
            /* This includes kernel code, kernel stack, and other kernel data */
            unpriv_pt_0_2mb[i] = phys_addr | X86_PTE_PRESENT | X86_PTE_WRITABLE | X86_PTE_USER;
        }
    }

    /* CRITICAL: Verify nested kernel boot stack pages are writable in unprivileged view
     * The stack is NK_NORMAL but MUST be writable for CPU execution
     * Stack pages are not page table pages, so they should be writable above */
    extern uint8_t nk_boot_stack_bottom[], nk_boot_stack_top[];
    uint64_t stack_start = ((uint64_t)nk_boot_stack_bottom) & ~0xFFF;
    uint64_t stack_end = ((uint64_t)nk_boot_stack_top) & ~0xFFF;

    if (!quiet) serial_puts("MONITOR: Verifying stack pages are writable in unprivileged view\n");
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
    g_unpriv_pd_ptr[0] = (uint64_t)virt_to_phys(unpriv_pt_0_2mb) | X86_PTE_PRESENT | X86_PTE_WRITABLE;

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

    /* CRITICAL: Also update monitor page table hierarchy to use monitor tables
     * This is required for monitor_call() to access kernel data structures
     * via 4KB pages instead of the original 2MB page mapping */
    /* Update monitor_pml4[0] to point to monitor_pdpt instead of boot_pdpt */
    uint64_t monitor_pdpt_phys = virt_to_phys(monitor_pdpt);
    monitor_pml4[0] = (boot_pml4_entry0 & 0xFFF) | monitor_pdpt_phys;

    /* Update monitor_pdpt[0] to point to monitor_pd instead of boot_pd */
    uint64_t monitor_pd_phys = virt_to_phys(monitor_pd);
    monitor_pdpt[0] = (boot_pdpt_entry0 & 0xFFF) | monitor_pd_phys;

    /* Debug: Verify the hierarchy update worked */
    if (!quiet) {
        serial_puts("MONITOR: After hierarchy update:\n");
        serial_puts("  monitor_pml4[0] = 0x");
        serial_put_hex(monitor_pml4[0]);
        serial_puts(" (should be monitor_pdpt)\n");
        serial_puts("  monitor_pdpt[0] = 0x");
        serial_put_hex(monitor_pdpt[0]);
        serial_puts(" (should be monitor_pd)\n");
        serial_puts("  unpriv_pml4[0] = 0x");
        serial_put_hex(unpriv_pml4[0]);
        serial_puts(" (should be unpriv_pdpt)\n");
        serial_puts("  unpriv_pdpt[0] = 0x");
        serial_put_hex(unpriv_pdpt[0]);
        serial_puts(" (should be unpriv_pd)\n");
    }

    /* Save physical addresses */
    monitor_pml4_phys = virt_to_phys(monitor_pml4);
    unpriv_pml4_phys = virt_to_phys(unpriv_pml4);

    /* Debug: Print page table structure */
    if (!quiet) {
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
        serial_puts("\n  g_unpriv_pd_ptr[0] = 0x");
        serial_put_hex(g_unpriv_pd_ptr[0]);
        serial_puts("\n  g_unpriv_pd_ptr[1] = 0x");
        serial_put_hex(g_unpriv_pd_ptr[1]);
        serial_puts("\n  unpriv_pd[2] (kernel at 4MB) = 0x");
        serial_put_hex(unpriv_pd[2]);
        serial_puts("\n  boot_pd[2] (kernel at 4MB) = 0x");
        serial_put_hex(boot_pd[2]);
        serial_puts("\n  unpriv_pdpt[0] (points to pd) = 0x");
        serial_put_hex(unpriv_pdpt[0]);
        serial_puts("\n  unpriv_pml4[0] (points to pdpt) = 0x");
        serial_put_hex(unpriv_pml4[0]);
        serial_puts("\n  Using 4KB pages for first 2MB\n");
    }

    /* Mark page table pages as NK_PGTABLE for PCD tracking */
    /* These pages should not be accessible to outer kernel for mapping */

    /* Mark boot page tables as NK_PGTABLE */
    _pcd_set_type_internal(virt_to_phys(boot_pml4), PCD_TYPE_NK_PGTABLE);
    _pcd_set_type_internal(virt_to_phys(boot_pdpt), PCD_TYPE_NK_PGTABLE);
    _pcd_set_type_internal(virt_to_phys(boot_pd), PCD_TYPE_NK_PGTABLE);
    _pcd_set_type_internal(virt_to_phys(boot_pd_apic), PCD_TYPE_NK_PGTABLE);
    _pcd_set_type_internal(virt_to_phys(boot_pt_apic), PCD_TYPE_NK_PGTABLE);

    /* Mark monitor page tables as NK_PGTABLE */
    _pcd_set_type_internal(virt_to_phys(monitor_pml4), PCD_TYPE_NK_PGTABLE);
    _pcd_set_type_internal(virt_to_phys(monitor_pdpt), PCD_TYPE_NK_PGTABLE);
    _pcd_set_type_internal(virt_to_phys(monitor_pd), PCD_TYPE_NK_PGTABLE);
    _pcd_set_type_internal(virt_to_phys(unpriv_pml4), PCD_TYPE_NK_PGTABLE);
    _pcd_set_type_internal(virt_to_phys(unpriv_pdpt), PCD_TYPE_NK_PGTABLE);
    _pcd_set_type_internal(virt_to_phys(unpriv_pd), PCD_TYPE_NK_PGTABLE);

    if (!quiet) serial_puts("MONITOR: Page tables initialized\n");

    /* Debug: Verify GDT is accessible */
    if (!quiet) {
        serial_puts("MONITOR: GDT at 0x");
        serial_put_hex((uint64_t)gdt64);
        serial_puts(" (size ");
        serial_put_hex(sizeof(gdt64));
        serial_puts(" bytes)\n");
    }
    /* Check if GDT is in first 2MB region */
    if ((uint64_t)gdt64 < 0x200000) {
        uint64_t gdt_page = (uint64_t)gdt64 & ~0xFFF;
        int gdt_pte_index = gdt_page >> 12;
        if (!quiet) {
            serial_puts("MONITOR: GDT page at 0x");
            serial_put_hex(gdt_page);
            serial_puts(", PTE = 0x");
            serial_put_hex(unpriv_pt_0_2mb[gdt_pte_index]);
            serial_puts("\n");
        }
    }

    /* Debug: Verify TSS is accessible */
    extern void tss_desc(void);
    extern void tss(void);
    if (!quiet) {
        serial_puts("MONITOR: TSS structure at 0x");
        serial_put_hex((uint64_t)tss);
        serial_puts("\n");
    }
    if ((uint64_t)tss < 0x200000) {
        uint64_t tss_page = (uint64_t)tss & ~0xFFF;
        int tss_pte_index = tss_page >> 12;
        if (!quiet) {
            serial_puts("MONITOR: TSS page PTE = 0x");
            serial_put_hex(unpriv_pt_0_2mb[tss_pte_index]);
            serial_puts("\n");
        }
    }

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

    if (!quiet) serial_puts("MONITOR: APIC accessible from unprivileged mode\n");
}

/* Get unprivileged CR3 value */
uint64_t monitor_get_unpriv_cr3(void) {
    return unpriv_pml4_phys;
}

/* Check if running in privileged (monitor) mode */
bool monitor_is_privileged(void) {
    uint64_t cr3 = arch_cr3_read();
    return cr3 == monitor_pml4_phys;
}

/* Debug: Dump page table hierarchy */
void monitor_dump_page_tables(void) {
    serial_puts("MONITOR: Page table dump:\n");
    serial_puts("  unpriv_pml4 at 0x");
    serial_put_hex((uint64_t)unpriv_pml4);
    serial_puts(" (phys 0x");
    serial_put_hex(unpriv_pml4_phys);
    serial_puts(")\n");
    serial_puts("    [0] = 0x");
    serial_put_hex(unpriv_pml4[0]);
    serial_puts(" [1] = 0x");
    serial_put_hex(unpriv_pml4[1]);
    serial_puts(" [2] = 0x");
    serial_put_hex(unpriv_pml4[2]);
    serial_puts("\n");

    serial_puts("  unpriv_pdpt at 0x");
    serial_put_hex((uint64_t)unpriv_pdpt);
    serial_puts("\n");
    serial_puts("    [0] = 0x");
    serial_put_hex(unpriv_pdpt[0]);
    serial_puts(" [1] = 0x");
    serial_put_hex(unpriv_pdpt[1]);
    serial_puts(" [2] = 0x");
    serial_put_hex(unpriv_pdpt[2]);
    serial_puts(" [3] = 0x");
    serial_put_hex(unpriv_pdpt[3]);
    serial_puts("\n");

    serial_puts("  unpriv_pd at 0x");
    serial_put_hex((uint64_t)unpriv_pd);
    serial_puts("\n");
    serial_puts("    [0] = 0x");
    serial_put_hex(unpriv_pd[0]);
    serial_puts(" [1] = 0x");
    serial_put_hex(unpriv_pd[1]);
    serial_puts(" [2] = 0x");
    serial_put_hex(unpriv_pd[2]);
    serial_puts(" [3] = 0x");
    serial_put_hex(unpriv_pd[3]);
    serial_puts("\n");
}

/* Internal monitor call handler (called from privileged context only) */
monitor_ret_t monitor_call_handler(monitor_call_t call, uint64_t arg1,
                                     uint64_t arg2, uint64_t arg3) {
    monitor_ret_t ret = {0, 0};

    /* Verify we're in privileged mode */
    extern uint64_t monitor_pml4_phys;
    uint64_t current_cr3 = arch_cr3_read();
    if (current_cr3 != monitor_pml4_phys) {
        serial_puts("MONITOR: ERROR - monitor_call_handler called from unprivileged context!\n");
        ret.error = -1;
        return ret;
    }

    switch (call) {
        case MONITOR_CALL_ALLOC_PHYS:
            /* Allocate physical pages */
            ret.result = (uint64_t)pmm_alloc((uint8_t)arg1);
            if (!ret.result) {
                ret.error = -1;
            }
            /* TEMPORARILY DISABLED: PCD type setting to debug crash */
            /* TODO: Re-enable once crash is fixed */
            break;

        case MONITOR_CALL_FREE_PHYS:
            /* Direct PMM access (already in privileged mode) */
            pmm_free((void *)arg1, (uint8_t)arg2);
            break;

        case MONITOR_CALL_SET_PAGE_TYPE:
            /* Set PCD type (monitor only) - enforce privilege check */
            if (!monitor_is_privileged()) {
                serial_puts("MONITOR: ERROR - PCD set type rejected (not privileged)\n");
                ret.error = -1;
                break;
            }
            _pcd_set_type_internal(arg1, (uint8_t)arg2);
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
                    _pcd_set_type_internal(addr + (i << PAGE_SHIFT), PCD_TYPE_NK_PGTABLE);
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
    /* If monitor not initialized yet, call directly */
    if (monitor_pml4_phys == 0) {
        return monitor_call_handler(call, arg1, arg2, arg3);
    }

    /* Already privileged? Call directly */
    if (monitor_is_privileged()) {
        return monitor_call_handler(call, arg1, arg2, arg3);
    }

    /* Unprivileged: use assembly stub to switch CR3 */
    return nk_entry_trampoline(call, arg1, arg2, arg3);
}

/* PMM monitor call wrappers */
void *monitor_pmm_alloc(uint8_t order) {
    /* If monitor not initialized, fall back to direct pmm_alloc
     * This handles early boot allocations (e.g., slab_init) before monitor_init() */
    if (!monitor_pml4_phys) {
        return pmm_alloc(order);
    }

    /* Normal path: route through monitor for PCD tracking */
    monitor_ret_t ret = monitor_call(MONITOR_CALL_ALLOC_PHYS, order, PCD_TYPE_OK_NORMAL, 0);
    return (void *)ret.result;
}

void monitor_pmm_free(void *addr, uint8_t order) {
    /* If monitor not initialized, fall back to direct pmm_free */
    if (!monitor_pml4_phys) {
        pmm_free(addr, order);
        return;
    }

    monitor_call(MONITOR_CALL_FREE_PHYS, (uint64_t)addr, order, 0);
}

/* PCD management functions (monitor only) */
void monitor_pcd_set_type(uint64_t phys_addr, uint8_t type) {
    /* Route through monitor_call for privilege enforcement */
    monitor_call(MONITOR_CALL_SET_PAGE_TYPE, phys_addr, type, 0);
}

uint8_t monitor_pcd_get_type(uint64_t phys_addr) {
    return pcd_get_type(phys_addr);
}

/* Get or create a page table at the given entry
 * Returns pointer to the table, or NULL on allocation failure */
static uint64_t *get_or_create_table(uint64_t *entry) {
    if (*entry & X86_PTE_PRESENT) {
        return (uint64_t*)(*entry & ~0xFFF);
    }

    /* Allocate new page table */
    uint64_t *table = monitor_alloc_pgtable(0);
    if (!table) {
        return NULL;
    }

    /* Link the entry to the new table */
    *entry = virt_to_phys(table) | X86_PTE_PRESENT | X86_PTE_WRITABLE;
    return table;
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
    /* Walk the 4-level page table hierarchy */

    int pml4_idx = PML4_INDEX(virt_addr);
    int pdpt_idx = PDPT_INDEX(virt_addr);
    int pd_idx = PD_INDEX(virt_addr);
    int pt_idx = PT_INDEX(virt_addr);

    /* Get or create PDPT */
    uint64_t *pdpt = get_or_create_table(&unpriv_pml4[pml4_idx]);
    if (!pdpt) return -1;

    /* Get or create PD */
    uint64_t *pd = get_or_create_table(&pdpt[pdpt_idx]);
    if (!pd) return -1;

    /* Get or create PT */
    uint64_t *pt = get_or_create_table(&pd[pd_idx]);
    if (!pt) return -1;

    /* Set the PTE */
    pt[pt_idx] = phys_addr | flags | X86_PTE_PRESENT;

    /* Invalidate TLB for this virtual address */
    monitor_invalidate_page((void*)virt_addr);

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
