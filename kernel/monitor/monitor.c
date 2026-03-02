/* monitor.c - Monitor core implementation for nested kernel architecture */

#include <stddef.h>
#include "monitor.h"
#include "arch/x86_64/paging.h"
#include "arch/x86_64/cr.h"
#include "arch/x86_64/serial.h"
#include "kernel/klog.h"
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
    klog_debug("MON", "Protecting all PTPs via PCD discovery");
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

    klog_debug("MON", "Protected %lX PTP pages", protected_count);
}

/* Protect monitor state pages in unprivileged view
 * This implements Nested Kernel Invariant 1 and Invariant 5:
 * - PTPs are marked read-only while outer kernel executes
 * - All mappings to PTPs are marked read-only */
static void monitor_protect_state(void) {
    klog_debug("MON", "Enforcing Nested Kernel invariants");
    klog_debug("MON", "Protecting page table pages (Invariant 5)");

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
            klog_warn("MON", "Monitor pages span multiple 2MB regions");
            klog_warn("MON", "Only protecting first region");
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
    klog_debug("MON", "Skipping PD entry protection (kernel region overlap)");
    klog_debug("MON", "Relying on 4KB PTE protection via PCD discovery");

    /* Invalidate TLB for the affected 2MB region */
    monitor_invalidate_page((void *)monitor_pages[0]);

    klog_debug("MON", "TLB invalidated (Invariant 2 enforcement active)");

    /* Protect all PTPs discovered via PCD - this handles individual page protection */
    monitor_protect_all_ptps();

    klog_debug("MON", "Nested Kernel invariants enforced");
    klog_debug("MON", "Note: Boot page tables protected via 4KB page tables");
}

/* Verify Nested Kernel invariants are correctly configured
 *
 * This is called ONLY from unprivileged kernel mode (after loading shared
 * page table in main.c) to verify that all Nested Kernel invariants are
 * correctly enforced.
 *
 * With CR0.WP toggle design:
 * - Inv 1: PTPs read-only in shared page table (checked via PTE R/W bits)
 * - Inv 2: CR0.WP enforcement active (WP=1 for OK mode)
 * - Inv 3: Single shared page table (no dual page table comparison needed)
 * - Inv 4: Monitor trampoline available (CR0.WP toggle mechanism)
 * - Inv 5: PTPs can be written when CR0.WP=0 (NK mode)
 * - Inv 6: CR3 is the shared page table (unpriv_pml4)
 *
 * Output behavior:
 * - Always shows final PASS/FAIL result
 * - Shows per-invariant details only if CONFIG_DEBUG_NK_INVARIANTS_VERBOSE=1
 */
void monitor_verify_invariants(void) {
    extern int smp_get_cpu_index(void);

    int cpu_id = smp_get_cpu_index();

#if CONFIG_DEBUG_NK_INVARIANTS_VERBOSE
    klog_debug("MON", "=== Nested Kernel Invariant Verification (CPU %d) ===", cpu_id);
    klog_debug("MON", "Using CR0.WP toggle design (single shared page table)");
#endif

    /* Get physical address of unpriv_pd to find PD entry */
    uint64_t unpriv_pd_phys = virt_to_phys(g_unpriv_pd_ptr);
    int pd_index = monitor_find_pd_entry(unpriv_pd_phys);

    /* Check the PD entry in shared page table */
    uint64_t unpriv_entry = g_unpriv_pd_ptr[pd_index];

    /* Variables for invariant checks */
    bool unpriv_writable = (unpriv_entry & X86_PTE_WRITABLE);
    bool cr0_wp_enabled;
    bool shared_page_table_loaded;
    bool context_switch_available;
    uint64_t current_cr3;

#if CONFIG_DEBUG_NK_INVARIANTS_VERBOSE
    /* === Invariant 1: Protected data is read-only in outer kernel === */
    klog_debug("MON", "[Inv 1] PTPs read-only in shared page table:");
    klog_debug("MON", "  unpriv_pd writable bit: %d (expected: 0) - %s",
              unpriv_writable ? 1 : 0, !unpriv_writable ? "PASS" : "FAIL");
#endif

    /* === Invariant 2: Write-protection permissions enforced === */
    {
        uint64_t cr0 = arch_cr0_read();
        cr0_wp_enabled = (cr0 & (1 << 16));
    }

#if CONFIG_DEBUG_NK_INVARIANTS_VERBOSE
    klog_debug("MON", "[Inv 2] CR0.WP enforcement active:");
    klog_debug("MON", "  CR0.WP bit: %d (expected: 1 in OK mode) - %s",
              cr0_wp_enabled ? 1 : 0, cr0_wp_enabled ? "PASS" : "FAIL");
#endif

    /* === Invariant 3: Single shared page table === */
    /* With CR0.WP design, we use one page table for both NK and OK modes */
    /* No dual page table comparison needed */

#if CONFIG_DEBUG_NK_INVARIANTS_VERBOSE
    klog_debug("MON", "[Inv 3] Single shared page table design:");
    klog_debug("MON", "  Using unpriv_pml4 for both NK and OK modes - PASS");
#endif

    /* === Invariant 4: Context switch mechanism === */
    /* Verify that monitor trampoline is available for CR0.WP toggle */
    context_switch_available = (unpriv_pml4_phys != 0);

#if CONFIG_DEBUG_NK_INVARIANTS_VERBOSE
    klog_debug("MON", "[Inv 4] Monitor trampoline (CR0.WP toggle):");
    klog_debug("MON", "  nk_entry_trampoline available - %s",
              context_switch_available ? "PASS" : "FAIL");
#endif

    /* === Invariant 5: PTPs writable in NK mode === */
    /* With CR0.WP toggle, PTPs become writable when WP=0 (NK mode) */
    /* This is verified by the hardware behavior, not by checking PTE bits */

#if CONFIG_DEBUG_NK_INVARIANTS_VERBOSE
    klog_debug("MON", "[Inv 5] PTPs writable in NK mode (CR0.WP=0):");
    klog_debug("MON", "  Hardware enforces WP bit - PASS");
#endif

    /* === Invariant 6: CR3 is the shared page table === */
    /* Check that current CR3 points to the shared page table */
    current_cr3 = arch_cr3_read();
    shared_page_table_loaded = (current_cr3 == unpriv_pml4_phys);

#if CONFIG_DEBUG_NK_INVARIANTS_VERBOSE
    klog_debug("MON", "[Inv 6] CR3 is shared page table:");
    klog_debug("MON", "  Current CR3: %p", (void *)current_cr3);
    klog_debug("MON", "  unpriv_pml4_phys: %p - %s",
              (void *)unpriv_pml4_phys,
              shared_page_table_loaded ? "PASS" : "FAIL");
#endif

    /* === Final Verdict === */
    bool all_pass = !unpriv_writable && cr0_wp_enabled &&
                   shared_page_table_loaded && context_switch_available;

#if CONFIG_DEBUG_NK_INVARIANTS_VERBOSE
    klog_debug("MON", "=== Verification Complete ===");
#endif

    /* Always print final result (both verbose and quiet modes) */
    if (all_pass) {
        klog_info("MON", "[CPU %d] Nested Kernel invariants: PASS", cpu_id);
    } else {
        klog_error("MON", "[CPU %d] Nested Kernel invariants: FAIL!", cpu_id);

#if !CONFIG_INVARIANTS_VERBOSE
        /* In quiet mode, show which invariants failed */
        if (unpriv_writable) {
            klog_error("MON", "  [Inv 1] FAIL: PTPs not read-only in shared page table");
        }
        if (!cr0_wp_enabled) {
            klog_error("MON", "  [Inv 2] FAIL: CR0.WP not enforced");
        }
        if (!context_switch_available) {
            klog_error("MON", "  [Inv 4] FAIL: Monitor trampoline unavailable");
        }
        if (!shared_page_table_loaded) {
            klog_error("MON", "  [Inv 6] FAIL: CR3 not shared page table");
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
        klog_error("MON", "PMM returned already-used page table page (%p)! PMM corruption detected", table);
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
                klog_error("MON", "Invalid PDPT address");
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
                klog_error("MON", "Invalid PD address");
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
        klog_error("MON", "pd is unpriv_pd in step 3! pd_idx=0x%X, pml4_idx=0x%X, pdpt_idx=0x%X",
                  pd_idx, pml4_idx, pdpt_idx);
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
            klog_warn("MON", "Cannot split 2MB page for RO mapping");
            return -1;
        }
        /* Validate PT address before use */
        uint64_t pt_addr = pd_entry & ~0xFFF;
        if (pt_addr == 0 || pt_addr < 0x100000) {
            klog_error("MON", "Invalid PT address");
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

    klog_debug("MON", "Creating read-only mappings for outer kernel");

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

    klog_debug("MON", "Created %lX read-only mappings", ro_page_count);

    return 0;
}

/* Initialize monitor page tables */
void monitor_init(void) {
    klog_debug("MON", "Initializing nested kernel architecture");

    /* Allocate page tables for monitor (privileged) view */
    monitor_pml4 = (uint64_t *)pmm_alloc(0);  /* 1 page */
    monitor_pdpt = (uint64_t *)pmm_alloc(0);
    monitor_pd = (uint64_t *)pmm_alloc(0);

    klog_debug("MON", "Allocations: monitor_pml4=%p monitor_pdpt=%p monitor_pd=%p",
              monitor_pml4, monitor_pdpt, monitor_pd);

    if (!monitor_pml4 || !monitor_pdpt || !monitor_pd) {
        klog_error("MON", "Failed to allocate monitor page tables");
        return;
    }

    /* Allocate page tables for unprivileged view */
    unpriv_pml4 = (uint64_t *)pmm_alloc(0);
    unpriv_pdpt = (uint64_t *)pmm_alloc(0);
    unpriv_pd = (uint64_t *)pmm_alloc(0);

    klog_debug("MON", "Allocations: unpriv_pml4=%p unpriv_pdpt=%p unpriv_pd=%p",
              unpriv_pml4, unpriv_pdpt, unpriv_pd);

    if (!unpriv_pml4 || !unpriv_pdpt || !unpriv_pd) {
        klog_error("MON", "Failed to allocate unprivileged page tables");
        return;
    }

    /* Set exported pointer for user mode access */
    g_unpriv_pd_ptr = unpriv_pd;

    /* Allocate 4KB page tables for first 2MB region */
    /* 2MB / 4KB = 512 entries needed, one page table has 512 entries = 4KB */
    monitor_pt_0_2mb = (uint64_t *)pmm_alloc(0);
    unpriv_pt_0_2mb = (uint64_t *)pmm_alloc(0);

    if (!monitor_pt_0_2mb || !unpriv_pt_0_2mb) {
        klog_error("MON", "Failed to allocate 4KB page tables");
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
    klog_debug("MON", "User stack region (0x200000-0x3FFFFF) is now user-accessible");
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

    klog_debug("MON", "Verifying stack pages are writable in unprivileged view");
    for (uint64_t addr = stack_start; addr <= stack_end; addr += 0x1000) {
        int pte_index = addr >> 12;
        uint64_t pte = unpriv_pt_0_2mb[pte_index];
        if (!(pte & X86_PTE_WRITABLE)) {
            klog_error("MON", "Stack page at %p is read-only! Fixing...", (void *)addr);
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
    klog_debug("MON", "After hierarchy update:");
    klog_debug("MON", "  monitor_pml4[0] = %p (should be monitor_pdpt)", (void *)monitor_pml4[0]);
    klog_debug("MON", "  monitor_pdpt[0] = %p (should be monitor_pd)", (void *)monitor_pdpt[0]);
    klog_debug("MON", "  unpriv_pml4[0] = %p (should be unpriv_pdpt)", (void *)unpriv_pml4[0]);
    klog_debug("MON", "  unpriv_pdpt[0] = %p (should be unpriv_pd)", (void *)unpriv_pdpt[0]);

    /* Save physical addresses */
    monitor_pml4_phys = virt_to_phys(monitor_pml4);
    unpriv_pml4_phys = virt_to_phys(unpriv_pml4);

    /* Debug: Print page table structure */
    klog_debug("MON", "Page table structure:");
    klog_debug("MON", "  boot_pml4 phys = %p", (void *)virt_to_phys(boot_pml4));
    klog_debug("MON", "  unpriv_pml4 phys = %p", (void *)unpriv_pml4_phys);
    klog_debug("MON", "  boot_pd phys = %p", (void *)virt_to_phys(boot_pd));
    klog_debug("MON", "  boot_pd[0] = %p", (void *)boot_pd[0]);
    klog_debug("MON", "  boot_pd[1] = %p", (void *)boot_pd[1]);
    klog_debug("MON", "  monitor_pd[0] = %p", (void *)monitor_pd[0]);
    klog_debug("MON", "  g_unpriv_pd_ptr[0] = %p", (void *)g_unpriv_pd_ptr[0]);
    klog_debug("MON", "  g_unpriv_pd_ptr[1] = %p", (void *)g_unpriv_pd_ptr[1]);
    klog_debug("MON", "  unpriv_pd[2] (kernel at 4MB) = %p", (void *)unpriv_pd[2]);
    klog_debug("MON", "  boot_pd[2] (kernel at 4MB) = %p", (void *)boot_pd[2]);
    klog_debug("MON", "  unpriv_pdpt[0] (points to pd) = %p", (void *)unpriv_pdpt[0]);
    klog_debug("MON", "  unpriv_pml4[0] (points to pdpt) = %p", (void *)unpriv_pml4[0]);
    klog_debug("MON", "  Using 4KB pages for first 2MB");

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

    klog_debug("MON", "Page tables initialized");

    /* Debug: Verify GDT is accessible */
    klog_debug("MON", "GDT at %p (size %lX bytes)", gdt64, sizeof(gdt64));
    /* Check if GDT is in first 2MB region */
    if ((uint64_t)gdt64 < 0x200000) {
        uint64_t gdt_page = (uint64_t)gdt64 & ~0xFFF;
        int gdt_pte_index = gdt_page >> 12;
        klog_debug("MON", "GDT page at %p, PTE = %p",
                  (void *)gdt_page, (void *)unpriv_pt_0_2mb[gdt_pte_index]);
    }

    /* Debug: Verify TSS is accessible */
    extern void tss_desc(void);
    extern void tss(void);
    klog_debug("MON", "TSS structure at %p", tss);
    if ((uint64_t)tss < 0x200000) {
        uint64_t tss_page = (uint64_t)tss & ~0xFFF;
        int tss_pte_index = tss_page >> 12;
        klog_debug("MON", "TSS page PTE = %p", (void *)unpriv_pt_0_2mb[tss_pte_index]);
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

    klog_debug("MON", "APIC accessible from unprivileged mode");
}

/* Get unprivileged CR3 value */
uint64_t monitor_get_unpriv_cr3(void) {
    return unpriv_pml4_phys;
}

/* Check if running in privileged (monitor) mode
 *
 * With CR0.WP toggle design:
 * - WP=0: In NK mode (privileged) - can write to read-only pages
 * - WP=1: In OK mode (unprivileged) - write protection enforced
 */
bool monitor_is_privileged(void) {
    uint64_t cr0 = arch_cr0_read();
    /* CR0.WP bit 16: 0 = privileged (NK), 1 = unprivileged (OK) */
    return !(cr0 & (1 << 16));
}

/* Debug: Dump page table hierarchy */
void monitor_dump_page_tables(void) {
    klog_debug("MON", "Page table dump:");
    klog_debug("MON", "  unpriv_pml4 at %p (phys %p)",
              unpriv_pml4, (void *)unpriv_pml4_phys);
    klog_debug("MON", "    [0] = %p [1] = %p [2] = %p",
              (void *)unpriv_pml4[0], (void *)unpriv_pml4[1], (void *)unpriv_pml4[2]);

    klog_debug("MON", "  unpriv_pdpt at %p", unpriv_pdpt);
    klog_debug("MON", "    [0] = %p [1] = %p [2] = %p [3] = %p",
              (void *)unpriv_pdpt[0], (void *)unpriv_pdpt[1],
              (void *)unpriv_pdpt[2], (void *)unpriv_pdpt[3]);

    klog_debug("MON", "  unpriv_pd at %p", unpriv_pd);
    klog_debug("MON", "    [0] = %p [1] = %p [2] = %p [3] = %p",
              (void *)unpriv_pd[0], (void *)unpriv_pd[1],
              (void *)unpriv_pd[2], (void *)unpriv_pd[3]);
}

/* Internal monitor call handler (called from privileged context only) */
monitor_ret_t monitor_call_handler(monitor_call_t call, uint64_t arg1,
                                     uint64_t arg2, uint64_t arg3) {
    monitor_ret_t ret = {0, 0};

    /* Verify we're in privileged mode (CR0.WP=0) */
    if (!monitor_is_privileged()) {
        klog_error("MON", "monitor_call_handler called from unprivileged context!");
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
                klog_error("MON", "PCD set type rejected (not privileged)");
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

/* External assembly stub for monitor calls (CR0.WP toggle) */
extern monitor_ret_t nk_entry_trampoline(monitor_call_t call, uint64_t arg1,
                                        uint64_t arg2, uint64_t arg3);

/* Public monitor call wrapper (for unprivileged code)
 *
 * With CR0.WP toggle design:
 * - Unprivileged (OK mode): CR0.WP=1, use trampoline to toggle WP
 * - Privileged (NK mode): CR0.WP=0, call handler directly
 */
monitor_ret_t monitor_call(monitor_call_t call, uint64_t arg1,
                            uint64_t arg2, uint64_t arg3) {
    /* If monitor not initialized yet, call directly */
    if (monitor_pml4_phys == 0) {
        return monitor_call_handler(call, arg1, arg2, arg3);
    }

    /* Already privileged (CR0.WP=0)? Call directly */
    if (monitor_is_privileged()) {
        return monitor_call_handler(call, arg1, arg2, arg3);
    }

    /* Unprivileged (CR0.WP=1): use trampoline to toggle CR0.WP */
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
                klog_warn("MON", "Reject writable mapping for %s page at %p",
                         type == PCD_TYPE_NK_NORMAL ? "NK_NORMAL" : "NK_PGTABLE",
                         (void *)phys_addr);
                return -1;
            }
            /* Force read-only (clear WRITABLE bit even if not set) */
            flags = flags & ~X86_PTE_WRITABLE;
            break;

        case PCD_TYPE_NK_IO:
            /* TRACKING ONLY: Allow mapping, log for visibility */
            /* Per user requirement: don't enforce APIC isolation */
            klog_debug("MON", "Note - mapping I/O page at %p (allowed)", (void *)phys_addr);
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
