/* Emergence Kernel - Kernel Virtual Memory Mapping Management
 *
 * Implementation of kernel-space memory region tracking for the
 * nested kernel architecture.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "kernel/kmap.h"
#include "kernel/pmm.h"
#include "kernel/slab.h"
#include "kernel/monitor/monitor.h"
#include "kernel/klog.h"
#include "arch/x86_64/paging.h"
#include "arch/x86_64/serial.h"
#include "arch/x86_64/cr.h"
#include <string.h>
#include "include/string.h"

/* ============================================================================
 * Global State
 * ============================================================================ */

/* Global kmap list - sorted by virtual address */
static struct list_head kmap_list;

/* Spinlock protecting kmap list modifications */
static spinlock_t kmap_lock;

/* Kmap subsystem initialized flag */
static bool kmap_initialized = false;

/* kmap_t allocation size (use standard slab allocator) */
#define KMAP_ALLOC_SIZE 128  /* Round up to next power of two */

/* Boot mapping constants */
#define BOOT_IDENTITY_MAP_SIZE    (2 * 1024 * 1024)  /* 2MB identity mapping */
#define BOOT_KERNEL_MAP_SIZE      (2 * 1024 * 1024)  /* 2MB kernel code mapping */

/* ============================================================================
 * Page Table Manipulation Helpers
 * ============================================================================ */

/**
 * get_pml4 - Get current PML4 table pointer
 *
 * Returns: Virtual address of current PML4 table
 */
static inline uint64_t *get_pml4(void) {
    uint64_t cr3;
    asm volatile ("mov %%cr3, %0" : "=r"(cr3));
    /* Convert physical CR3 to virtual address using higher-half mapping */
    return (uint64_t *)(uint64_t)PA_TO_VA(cr3 & ~0xFFF);
}

/**
 * get_pt_index - Get page table index for a virtual address
 * @addr: Virtual address
 * @level: Page table level (0=PT, 1=PD, 2=PDPT, 3=PML4)
 *
 * Returns: Index into the page table at the specified level
 */
static inline uint64_t get_pt_index(uint64_t addr, int level) {
    int shift = 12 + level * 9;
    return (addr >> shift) & 0x1FF;
}

/**
 * pte_set_flags - Set flags in a page table entry
 * @pte: Pointer to page table entry
 * @flags: Flags to set (X86_PTE_*)
 */
static inline void pte_set_flags(uint64_t *pte, uint64_t flags) {
    *pte = (*pte & 0xFFFFFFF000000000ULL) | (flags & 0x0000000FFFFFFFFFULL);
}

/**
 * pte_get_phys - Get physical address from page table entry
 * @pte: Page table entry value
 *
 * Returns: Physical address (page-aligned)
 */
static inline uint64_t pte_get_phys(uint64_t pte) {
    return pte & X86_PTE_PHYS_MASK;
}

/* ============================================================================
 * Internal Helpers
 * ============================================================================ */

/**
 * kmap_alloc_page - Allocate and map a single page
 * @virt_addr: Virtual address to map
 * @flags: PTE flags for the mapping
 *
 * Returns: 0 on success, negative error code on failure
 *
 * Allocates a physical page from PMM and creates the page table
 * entries to map it to the specified virtual address.
 *
 * NOTE: This function allocates intermediate page tables as needed.
 * On failure, all previously allocated resources are cleaned up.
 */
static int kmap_alloc_page(uint64_t virt_addr, uint64_t flags) {
    uint64_t *pml4, *pdpt, *pd, *pt;
    uint64_t phys;
    void *pdpt_page = NULL;
    void *pd_page = NULL;
    void *pt_page = NULL;
    bool pdpt_created = false;
    bool pd_created = false;

    /* Allocate physical page */
    void *page = pmm_alloc(0);  /* order 0 = 1 page */
    if (page == NULL) {
        return -1;  /* Out of memory */
    }
    phys = (uint64_t)page;

    /* Get PML4 */
    pml4 = get_pml4();

    /* Walk page tables, creating missing entries */
    uint64_t pml4_idx = get_pt_index(virt_addr, 3);
    uint64_t pdpt_idx = get_pt_index(virt_addr, 2);
    uint64_t pd_idx = get_pt_index(virt_addr, 1);
    uint64_t pt_idx = get_pt_index(virt_addr, 0);

    /* Check/create PDPT */
    if (!(pml4[pml4_idx] & X86_PTE_PRESENT)) {
        pdpt_page = pmm_alloc(0);
        if (pdpt_page == NULL) {
            pmm_free(page, 0);
            return -1;
        }
        /* Clear new page table */
        memset(PA_TO_VA((uint64_t)pdpt_page), 0, PAGE_SIZE);
        pml4[pml4_idx] = (uint64_t)pdpt_page | X86_PTE_PRESENT | X86_PTE_WRITABLE | X86_PTE_USER;
        pdpt_created = true;
    }
    pdpt = (uint64_t *)PA_TO_VA(pte_get_phys(pml4[pml4_idx]));

    /* Check/create PD */
    if (!(pdpt[pdpt_idx] & X86_PTE_PRESENT)) {
        pd_page = pmm_alloc(0);
        if (pd_page == NULL) {
            /* Rollback: clear PDPT entry and free PDPT if we created it */
            if (pdpt_created) {
                pml4[pml4_idx] = 0;
                pmm_free(pdpt_page, 0);
            }
            pmm_free(page, 0);
            return -1;
        }
        memset(PA_TO_VA((uint64_t)pd_page), 0, PAGE_SIZE);
        pdpt[pdpt_idx] = (uint64_t)pd_page | X86_PTE_PRESENT | X86_PTE_WRITABLE | X86_PTE_USER;
        pd_created = true;
    }
    pd = (uint64_t *)PA_TO_VA(pte_get_phys(pdpt[pdpt_idx]));

    /* Check/create PT */
    if (!(pd[pd_idx] & X86_PTE_PRESENT)) {
        pt_page = pmm_alloc(0);
        if (pt_page == NULL) {
            /* Rollback: clear PD entry and free PD if we created it */
            if (pd_created) {
                pdpt[pdpt_idx] = 0;
                pmm_free(pd_page, 0);
            }
            if (pdpt_created) {
                pml4[pml4_idx] = 0;
                pmm_free(pdpt_page, 0);
            }
            pmm_free(page, 0);
            return -1;
        }
        memset(PA_TO_VA((uint64_t)pt_page), 0, PAGE_SIZE);
        pd[pd_idx] = (uint64_t)pt_page | X86_PTE_PRESENT | X86_PTE_WRITABLE | X86_PTE_USER;
    }
    pt = (uint64_t *)PA_TO_VA(pte_get_phys(pd[pd_idx]));

    /* Set PTE */
    pt[pt_idx] = phys | flags;

    /* Invalidate TLB */
    arch_tlb_invalidate_page((void *)virt_addr);

    return 0;
}

/**
 * kmap_free_page - Unmap and free a single page
 * @virt_addr: Virtual address to unmap
 *
 * Returns: Physical address of the freed page, or 0 on failure
 *
 * Clears the page table entry and returns the physical page to PMM.
 * Also cleans up empty page tables to prevent memory leaks.
 */
static uint64_t kmap_free_page(uint64_t virt_addr) {
    uint64_t *pml4, *pdpt, *pd, *pt;
    uint64_t phys;
    int i;

    pml4 = get_pml4();

    uint64_t pml4_idx = get_pt_index(virt_addr, 3);
    uint64_t pdpt_idx = get_pt_index(virt_addr, 2);
    uint64_t pd_idx = get_pt_index(virt_addr, 1);
    uint64_t pt_idx = get_pt_index(virt_addr, 0);

    /* Walk page tables */
    if (!(pml4[pml4_idx] & X86_PTE_PRESENT)) {
        return 0;  /* Not mapped */
    }
    pdpt = (uint64_t *)PA_TO_VA(pte_get_phys(pml4[pml4_idx]));

    if (!(pdpt[pdpt_idx] & X86_PTE_PRESENT)) {
        return 0;  /* Not mapped */
    }
    pd = (uint64_t *)PA_TO_VA(pte_get_phys(pdpt[pdpt_idx]));

    if (!(pd[pd_idx] & X86_PTE_PRESENT)) {
        return 0;  /* Not mapped */
    }
    pt = (uint64_t *)PA_TO_VA(pte_get_phys(pd[pd_idx]));

    if (!(pt[pt_idx] & X86_PTE_PRESENT)) {
        return 0;  /* Not mapped */
    }

    /* Get physical address before clearing PTE */
    phys = pte_get_phys(pt[pt_idx]);

    /* Clear PTE */
    pt[pt_idx] = 0;

    /* Invalidate TLB */
    arch_tlb_invalidate_page((void *)virt_addr);

    /* Return physical page to PMM */
    pmm_free((void *)phys, 0);

    /* Check if PT is now empty - free it if so */
    bool pt_empty = true;
    for (i = 0; i < 512; i++) {
        if (pt[i] & X86_PTE_PRESENT) {
            pt_empty = false;
            break;
        }
    }
    if (pt_empty) {
        /* PT is empty, free it */
        uint64_t pt_phys = pte_get_phys(pd[pd_idx]);
        pd[pd_idx] = 0;
        pmm_free((void *)pt_phys, 0);

        /* Check if PD is now empty */
        bool pd_empty = true;
        for (i = 0; i < 512; i++) {
            if (pd[i] & X86_PTE_PRESENT) {
                pd_empty = false;
                break;
            }
        }
        if (pd_empty) {
            /* PD is empty, free it */
            uint64_t pd_phys = pte_get_phys(pdpt[pdpt_idx]);
            pdpt[pdpt_idx] = 0;
            pmm_free((void *)pd_phys, 0);

            /* Check if PDPT is now empty */
            bool pdpt_empty = true;
            for (i = 0; i < 512; i++) {
                if (pdpt[i] & X86_PTE_PRESENT) {
                    pdpt_empty = false;
                    break;
                }
            }
            if (pdpt_empty) {
                /* PDPT is empty, free it */
                uint64_t pdpt_phys = pte_get_phys(pml4[pml4_idx]);
                pml4[pml4_idx] = 0;
                pmm_free((void *)pdpt_phys, 0);
            }
        }
    }

    return phys;
}

/* ============================================================================
 * Public API - Initialization
 * ============================================================================ */

/**
 * kmap_init - Initialize kmap subsystem and register boot mappings
 *
 * Must be called after PMM and slab allocator are initialized.
 * Registers all boot mappings created during kernel initialization.
 */
void kmap_init(void) {
    /* Initialize list head */
    list_init(&kmap_list);

    /* Initialize spinlock */
    spin_lock_init(&kmap_lock);

    /* Register boot mappings */
    /* These mappings are created during boot in boot.S */

    /* 1. Identity mapping (0-2MB) - Non-pageable device/memory region */
    kmap_create(0x0, BOOT_IDENTITY_MAP_SIZE,
                0x0,
                X86_PTE_PRESENT | X86_PTE_WRITABLE,
                KMAP_DEVICE,
                KMAP_NONPAGEABLE,
                KMAP_MONITOR_NONE,
                "identity_low");

    /* 2. Higher-half kernel mapping (0xFFFFFFFF80000000+) - Code and data */
    kmap_create(KERNEL_BASE_VA, KERNEL_BASE_VA + BOOT_KERNEL_MAP_SIZE,
                KERNEL_BASE_PA,
                X86_PTE_PRESENT | X86_PTE_WRITABLE,
                KMAP_CODE,
                KMAP_NONPAGEABLE,
                KMAP_MONITOR_READONLY,
                "kernel_code");

    kmap_initialized = true;

    klog_info("KMAP", "initialized with boot mappings");
}

/* ============================================================================
 * Public API - Core Operations
 * ============================================================================ */

/**
 * kmap_create - Create a new kernel mapping
 */
kmap_t *kmap_create(uint64_t virt_start, uint64_t virt_end,
                    uint64_t phys_base, uint64_t flags,
                    kmap_type_t type, kmap_pageable_t pageable,
                    kmap_monitor_t monitor, const char *name) {
    kmap_t *mapping;

    /* Validate parameters */
    if (!PAGE_ALIGNED(virt_start) || !PAGE_ALIGNED(virt_end)) {
        return NULL;
    }
    if (virt_start >= virt_end) {
        return NULL;
    }

    /* Allocate kmap structure using standard slab allocator */
    mapping = (kmap_t *)slab_alloc_size(KMAP_ALLOC_SIZE);
    if (mapping == NULL) {
        return NULL;
    }

    /* Initialize fields */
    mapping->virt_start = virt_start;
    mapping->virt_end = virt_end;
    mapping->phys_base = phys_base;
    mapping->type = type;
    mapping->pageable = pageable;
    mapping->monitor = monitor;
    mapping->flags = flags;
    mapping->refcount = 1;
    mapping->num_pages = (virt_end - virt_start) / PAGE_SIZE;

    /* Copy name */
    if (name != NULL) {
        strncpy(mapping->name, name, sizeof(mapping->name) - 1);
        mapping->name[sizeof(mapping->name) - 1] = '\0';
    } else {
        mapping->name[0] = '\0';
    }

    /* Add to global list (sorted by address) */
    irq_flags_t irq_flags = spin_lock_irqsave(&kmap_lock);

    /* Find insertion point */
    struct list_head *pos;
    list_for_each(pos, &kmap_list) {
        kmap_t *entry = list_entry(pos, kmap_t, node);
        if (virt_start < entry->virt_start) {
            /* Insert before this entry */
            list_push_front(&entry->node, &mapping->node);
            goto inserted;
        }
    }

    /* Add to end if no earlier insertion point found */
    list_push_back(&kmap_list, &mapping->node);

inserted:
    spin_unlock_irqrestore(&kmap_lock, irq_flags);

    return mapping;
}

/**
 * kmap_destroy - Destroy a kernel mapping
 *
 * Returns: 0 on success, negative error code on failure
 *
 * Removes the mapping from the list and frees associated resources.
 * Returns error if refcount is not exactly zero.
 */
int kmap_destroy(kmap_t *mapping) {
    if (mapping == NULL) {
        return -1;
    }

    /* Check reference count - must be exactly zero to destroy */
    if (mapping->refcount != 0) {
        return -1;  /* Still referenced (or over-freed if negative) */
    }

    /* Remove from list */
    irq_flags_t irq_flags = spin_lock_irqsave(&kmap_lock);
    list_remove(&mapping->node);
    spin_unlock_irqrestore(&kmap_lock, irq_flags);

    /* Free structure using standard slab allocator */
    slab_free_size(mapping, KMAP_ALLOC_SIZE);

    return 0;
}

/**
 * kmap_get - Increment reference count
 */
void kmap_get(kmap_t *mapping) {
    if (mapping != NULL) {
        __atomic_add_fetch(&mapping->refcount, 1, __ATOMIC_SEQ_CST);
    }
}

/**
 * kmap_put - Decrement reference count and free if zero
 */
void kmap_put(kmap_t *mapping) {
    if (mapping == NULL) {
        return;
    }

    int32_t old = __atomic_fetch_sub(&mapping->refcount, 1, __ATOMIC_SEQ_CST);
    if (old == 1) {
        /* Refcount reached zero, destroy */
        kmap_destroy(mapping);
    }
}

/**
 * kmap_modify_flags - Modify mapping flags
 *
 * Returns: 0 on success, negative error code on failure
 *
 * Updates the PTE flags for all pages in the mapping.
 * Invalidates TLB entries as needed.
 *
 * NOTE: Updates both the mapping->flags and actual page table entries
 * atomically to prevent race conditions.
 */
int kmap_modify_flags(kmap_t *mapping, uint64_t new_flags) {
    uint64_t addr;

    if (mapping == NULL) {
        return -1;
    }

    /* Update page table entries for all pages in mapping */
    irq_flags_t irq_flags = spin_lock_irqsave(&kmap_lock);

    /* Update mapping flags atomically */
    mapping->flags = new_flags;

    for (addr = mapping->virt_start; addr < mapping->virt_end; addr += PAGE_SIZE) {
        uint64_t *pml4, *pdpt, *pd, *pt;
        uint64_t pml4_idx = get_pt_index(addr, 3);
        uint64_t pdpt_idx = get_pt_index(addr, 2);
        uint64_t pd_idx = get_pt_index(addr, 1);
        uint64_t pt_idx = get_pt_index(addr, 0);

        pml4 = get_pml4();

        if (!(pml4[pml4_idx] & X86_PTE_PRESENT)) {
            continue;
        }
        pdpt = (uint64_t *)PA_TO_VA(pte_get_phys(pml4[pml4_idx]));

        if (!(pdpt[pdpt_idx] & X86_PTE_PRESENT)) {
            continue;
        }
        pd = (uint64_t *)PA_TO_VA(pte_get_phys(pdpt[pdpt_idx]));

        if (!(pd[pd_idx] & X86_PTE_PRESENT)) {
            continue;
        }
        pt = (uint64_t *)PA_TO_VA(pte_get_phys(pd[pd_idx]));

        if (pt[pt_idx] & X86_PTE_PRESENT) {
            /* Preserve physical address, update flags */
            uint64_t phys = pte_get_phys(pt[pt_idx]);
            pt[pt_idx] = phys | new_flags;
            /* Invalidate TLB */
            arch_tlb_invalidate_page((void *)addr);
        }
    }

    spin_unlock_irqrestore(&kmap_lock, irq_flags);

    return 0;
}

/**
 * kmap_split - Split a mapping at address
 */
int kmap_split(kmap_t *mapping, uint64_t split_addr) {
    kmap_t *new_mapping;

    if (mapping == NULL) {
        return -1;
    }

    /* Validate split address */
    if (!PAGE_ALIGNED(split_addr)) {
        return -1;
    }
    if (split_addr <= mapping->virt_start || split_addr >= mapping->virt_end) {
        return -1;
    }

    /* Create new mapping for upper half */
    new_mapping = (kmap_t *)slab_alloc_size(KMAP_ALLOC_SIZE);
    if (new_mapping == NULL) {
        return -1;
    }

    /* Copy properties */
    new_mapping->virt_start = split_addr;
    new_mapping->virt_end = mapping->virt_end;
    new_mapping->phys_base = mapping->phys_base + (split_addr - mapping->virt_start);
    new_mapping->type = mapping->type;
    new_mapping->pageable = mapping->pageable;
    new_mapping->monitor = mapping->monitor;
    new_mapping->flags = mapping->flags;
    new_mapping->refcount = 1;
    new_mapping->num_pages = (mapping->virt_end - split_addr) / PAGE_SIZE;
    strncpy(new_mapping->name, mapping->name, sizeof(new_mapping->name));

    /* Modify original mapping */
    irq_flags_t irq_flags = spin_lock_irqsave(&kmap_lock);
    mapping->virt_end = split_addr;
    mapping->num_pages = (split_addr - mapping->virt_start) / PAGE_SIZE;

    /* Insert new mapping after original */
    list_push_front(&mapping->node, &new_mapping->node);
    spin_unlock_irqrestore(&kmap_lock, irq_flags);

    return 0;
}

/**
 * kmap_merge - Merge adjacent compatible mappings
 *
 * Returns: 0 on success, negative error code on failure
 *
 * Merges two adjacent mappings if they have identical properties.
 * The second mapping is removed from the list.
 *
 * NOTE: Both mappings must have refcount of exactly 1 to merge.
 * This prevents merging mappings that are actively in use.
 */
int kmap_merge(kmap_t *m1, kmap_t *m2) {
    if (m1 == NULL || m2 == NULL) {
        return -1;
    }

    /* Check if mappings are adjacent and compatible */
    if (m1->virt_end != m2->virt_start) {
        return -1;  /* Not adjacent */
    }
    if (m1->type != m2->type ||
        m1->pageable != m2->pageable ||
        m1->monitor != m2->monitor ||
        m1->flags != m2->flags) {
        return -1;  /* Properties don't match */
    }

    /* Check refcounts - both must be exactly 1 to merge safely */
    if (m1->refcount != 1 || m2->refcount != 1) {
        return -1;  /* Mappings are in use, cannot merge */
    }

    /* Merge m2 into m1 */
    irq_flags_t irq_flags = spin_lock_irqsave(&kmap_lock);
    m1->virt_end = m2->virt_end;
    m1->num_pages += m2->num_pages;
    list_remove(&m2->node);
    spin_unlock_irqrestore(&kmap_lock, irq_flags);

    /* Free m2 structure using standard slab allocator */
    slab_free_size(m2, KMAP_ALLOC_SIZE);

    return 0;
}

/* ============================================================================
 * Public API - Lookup
 * ============================================================================ */

/**
 * kmap_lookup - Find mapping containing address
 *
 * Returns: Pointer to kmap_t with incremented refcount, or NULL if not found
 *
 * Searches the kmap list for a region containing the specified address.
 * The returned mapping has its reference count incremented - the caller
 * MUST call kmap_put() when done using the pointer.
 *
 * This refcounting prevents use-after-free if another thread destroys
 * the mapping between lookup and use.
 */
kmap_t *kmap_lookup(uint64_t addr) {
    kmap_t *result = NULL;
    irq_flags_t irq_flags = spin_lock_irqsave(&kmap_lock);

    struct list_head *pos;
    list_for_each(pos, &kmap_list) {
        kmap_t *entry = list_entry(pos, kmap_t, node);
        if (addr >= entry->virt_start && addr < entry->virt_end) {
            result = entry;
            /* Increment refcount while holding lock to prevent race */
            __atomic_add_fetch(&result->refcount, 1, __ATOMIC_SEQ_CST);
            break;
        }
    }

    spin_unlock_irqrestore(&kmap_lock, irq_flags);

    return result;
}

/**
 * kmap_lookup_range - Find mapping overlapping range
 *
 * Returns: Pointer to kmap_t with incremented refcount, or NULL if not found
 *
 * Finds any mapping that overlaps the specified range.
 * The returned mapping has its reference count incremented - the caller
 * MUST call kmap_put() when done using the pointer.
 */
kmap_t *kmap_lookup_range(uint64_t start, uint64_t end) {
    kmap_t *result = NULL;
    irq_flags_t irq_flags = spin_lock_irqsave(&kmap_lock);

    struct list_head *pos;
    list_for_each(pos, &kmap_list) {
        kmap_t *entry = list_entry(pos, kmap_t, node);
        /* Check for overlap */
        if (start < entry->virt_end && end > entry->virt_start) {
            result = entry;
            /* Increment refcount while holding lock to prevent race */
            __atomic_add_fetch(&result->refcount, 1, __ATOMIC_SEQ_CST);
            break;
        }
    }

    spin_unlock_irqrestore(&kmap_lock, irq_flags);

    return result;
}

/**
 * kmap_is_pageable - Query pageable status of address
 *
 * Returns: true if address is in a pageable region
 */
bool kmap_is_pageable(uint64_t addr) {
    kmap_t *mapping = kmap_lookup(addr);
    if (mapping == NULL) {
        return false;  /* Unknown region, assume non-pageable for safety */
    }
    bool pageable = (mapping->pageable == KMAP_PAGEABLE);
    kmap_put(mapping);  /* Release refcount from kmap_lookup */
    return pageable;
}

/**
 * kmap_get_monitor_protection - Query monitor protection level
 *
 * Returns: Monitor protection level for the address
 */
kmap_monitor_t kmap_get_monitor_protection(uint64_t addr) {
    kmap_t *mapping = kmap_lookup(addr);
    if (mapping == NULL) {
        return KMAP_MONITOR_NONE;
    }
    kmap_monitor_t protection = mapping->monitor;
    kmap_put(mapping);  /* Release refcount from kmap_lookup */
    return protection;
}

/* ============================================================================
 * Public API - Page Table Operations
 * ============================================================================ */

/**
 * kmap_map_pages_direct - Map a page with specific physical address
 * @virt_addr: Virtual address to map
 * @phys_addr: Physical address to map (must be page-aligned)
 * @flags: PTE flags for the mapping
 *
 * Returns: 0 on success, negative error code on failure
 *
 * Similar to kmap_alloc_page but uses a pre-allocated physical page
 * instead of allocating from PMM. Used for boot mappings and MMIO.
 */
static int kmap_map_pages_direct(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
    uint64_t *pml4, *pdpt, *pd, *pt;
    void *pdpt_page = NULL;
    void *pd_page = NULL;
    void *pt_page = NULL;
    bool pdpt_created = false;
    bool pd_created = false;

    /* Validate physical address is page-aligned */
    if ((phys_addr & (PAGE_SIZE - 1)) != 0) {
        return -1;
    }

    /* Get PML4 */
    pml4 = get_pml4();

    /* Walk page tables, creating missing entries */
    uint64_t pml4_idx = get_pt_index(virt_addr, 3);
    uint64_t pdpt_idx = get_pt_index(virt_addr, 2);
    uint64_t pd_idx = get_pt_index(virt_addr, 1);
    uint64_t pt_idx = get_pt_index(virt_addr, 0);

    /* Check/create PDPT */
    if (!(pml4[pml4_idx] & X86_PTE_PRESENT)) {
        pdpt_page = pmm_alloc(0);
        if (pdpt_page == NULL) {
            return -1;
        }
        memset(PA_TO_VA((uint64_t)pdpt_page), 0, PAGE_SIZE);
        pml4[pml4_idx] = (uint64_t)pdpt_page | X86_PTE_PRESENT | X86_PTE_WRITABLE | X86_PTE_USER;
        pdpt_created = true;
    }
    pdpt = (uint64_t *)PA_TO_VA(pte_get_phys(pml4[pml4_idx]));

    /* Check/create PD */
    if (!(pdpt[pdpt_idx] & X86_PTE_PRESENT)) {
        pd_page = pmm_alloc(0);
        if (pd_page == NULL) {
            if (pdpt_created) {
                pml4[pml4_idx] = 0;
                pmm_free(pdpt_page, 0);
            }
            return -1;
        }
        memset(PA_TO_VA((uint64_t)pd_page), 0, PAGE_SIZE);
        pdpt[pdpt_idx] = (uint64_t)pd_page | X86_PTE_PRESENT | X86_PTE_WRITABLE | X86_PTE_USER;
        pd_created = true;
    }
    pd = (uint64_t *)PA_TO_VA(pte_get_phys(pdpt[pdpt_idx]));

    /* Check/create PT */
    if (!(pd[pd_idx] & X86_PTE_PRESENT)) {
        pt_page = pmm_alloc(0);
        if (pt_page == NULL) {
            if (pd_created) {
                pdpt[pdpt_idx] = 0;
                pmm_free(pd_page, 0);
            }
            if (pdpt_created) {
                pml4[pml4_idx] = 0;
                pmm_free(pdpt_page, 0);
            }
            return -1;
        }
        memset(PA_TO_VA((uint64_t)pt_page), 0, PAGE_SIZE);
        pd[pd_idx] = (uint64_t)pt_page | X86_PTE_PRESENT | X86_PTE_WRITABLE | X86_PTE_USER;
    }
    pt = (uint64_t *)PA_TO_VA(pte_get_phys(pd[pd_idx]));

    /* Set PTE with the specified physical address */
    pt[pt_idx] = phys_addr | flags;

    /* Invalidate TLB */
    arch_tlb_invalidate_page((void *)virt_addr);

    return 0;
}

/**
 * kmap_map_pages - Map pages for a kmap region
 *
 * Returns: 0 on success, negative error code on failure
 *
 * Creates page table entries for all pages in the mapping.
 * For mappings with phys_base != 0, uses contiguous physical memory.
 * For mappings with phys_base == 0, allocates physical pages on demand.
 *
 * On failure, unmaps all previously mapped pages before returning.
 */
int kmap_map_pages(kmap_t *mapping) {
    uint64_t addr;
    uint64_t last_mapped_addr = mapping->virt_start;
    int ret = 0;

    if (mapping == NULL) {
        return -1;
    }

    /* Map each page in the region */
    for (addr = mapping->virt_start; addr < mapping->virt_end; addr += PAGE_SIZE) {
        if (mapping->phys_base != 0) {
            /* Direct mapping: use contiguous physical memory */
            uint64_t phys = mapping->phys_base + (addr - mapping->virt_start);
            ret = kmap_map_pages_direct(addr, phys, mapping->flags);
        } else {
            /* Allocate-on-demand for pageable regions */
            ret = kmap_alloc_page(addr, mapping->flags);
        }

        if (ret != 0) {
            /* Allocation failed, unmap previously mapped pages */
            for (uint64_t cleanup_addr = mapping->virt_start;
                 cleanup_addr < last_mapped_addr;
                 cleanup_addr += PAGE_SIZE) {
                kmap_free_page(cleanup_addr);
            }
            return -1;
        }
        last_mapped_addr = addr + PAGE_SIZE;
    }

    return 0;
}

/**
 * kmap_unmap_pages - Unmap pages for a kmap region
 */
int kmap_unmap_pages(kmap_t *mapping) {
    uint64_t addr;

    if (mapping == NULL) {
        return -1;
    }

    /* Unmap each page in the region */
    for (addr = mapping->virt_start; addr < mapping->virt_end; addr += PAGE_SIZE) {
        kmap_free_page(addr);
    }

    return 0;
}

/* ============================================================================
 * Public API - Demand Paging
 * ============================================================================ */

/**
 * kmap_handle_page_fault - Handle page fault in pageable region
 *
 * Returns: 0 on success, negative error code on failure
 *
 * Called from page_fault_handler() to handle faults in pageable regions.
 * Allocates a physical page, maps it, and returns.
 * Returns error if fault is in non-pageable region (shouldn't happen).
 *
 * IMPORTANT: This function uses an atomic check-and-allocate pattern
 * to prevent race conditions when multiple CPUs fault on the same page.
 */
int kmap_handle_page_fault(uint64_t fault_addr, uint64_t error_code) {
    kmap_t *mapping;

    /* Look up the faulting address */
    mapping = kmap_lookup(fault_addr);
    if (mapping == NULL) {
        /* Address not in any kmap region */
        return -1;
    }

    /* Check if region is pageable */
    if (mapping->pageable != KMAP_PAGEABLE) {
        /* Page fault in non-pageable region - this is a bug */
        klog_info("KMAP", "page fault in non-pageable region %s at 0x%lx",
                  mapping->name, fault_addr);
        kmap_put(mapping);
        return -1;
    }

    /* Align fault address to page boundary */
    uint64_t page_addr = fault_addr & ~0xFFF;

    /* Check if page is already mapped (race with another CPU) */
    uint64_t *pml4 = get_pml4();
    uint64_t pml4_idx = get_pt_index(page_addr, 3);
    uint64_t pdpt_idx = get_pt_index(page_addr, 2);
    uint64_t pd_idx = get_pt_index(page_addr, 1);
    uint64_t pt_idx = get_pt_index(page_addr, 0);

    bool already_mapped = false;
    if ((pml4[pml4_idx] & X86_PTE_PRESENT)) {
        uint64_t *pdpt = (uint64_t *)PA_TO_VA(pte_get_phys(pml4[pml4_idx]));
        if ((pdpt[pdpt_idx] & X86_PTE_PRESENT)) {
            uint64_t *pd = (uint64_t *)PA_TO_VA(pte_get_phys(pdpt[pdpt_idx]));
            if ((pd[pd_idx] & X86_PTE_PRESENT)) {
                uint64_t *pt = (uint64_t *)PA_TO_VA(pte_get_phys(pd[pd_idx]));
                if ((pt[pt_idx] & X86_PTE_PRESENT)) {
                    already_mapped = true;
                }
            }
        }
    }

    if (already_mapped) {
        /* Another CPU already mapped this page */
        klog_info("KMAP", "page already mapped at 0x%lx (race detected)", fault_addr);
        kmap_put(mapping);
        return 0;  /* Success - page is mapped */
    }

    /* Allocate and map the page */
    int ret = kmap_alloc_page(page_addr, mapping->flags);
    if (ret != 0) {
        klog_info("KMAP", "failed to allocate page for fault at 0x%lx", fault_addr);
        kmap_put(mapping);
        return -1;
    }

    klog_info("KMAP", "allocated page for fault at 0x%lx", fault_addr);

    kmap_put(mapping);  /* Release refcount from kmap_lookup */

    return 0;
}

/* ============================================================================
 * Public API - Debugging
 * ============================================================================ */

/**
 * kmap_dump - Dump all kmap entries to serial console
 */
void kmap_dump(void) {
    irq_flags_t irq_flags = spin_lock_irqsave(&kmap_lock);

    klog_info("KMAP", "dump:");
    klog_info("KMAP", "  %-18s %-18s %-12s %-10s %-10s %s",
              "start", "end", "type", "pageable", "monitor", "name");

    struct list_head *pos;
    list_for_each(pos, &kmap_list) {
        kmap_t *entry = list_entry(pos, kmap_t, node);

        /* Type string */
        const char *type_str;
        switch (entry->type) {
            case KMAP_CODE:      type_str = "CODE"; break;
            case KMAP_RODATA:    type_str = "RODATA"; break;
            case KMAP_DATA:      type_str = "DATA"; break;
            case KMAP_STACK:     type_str = "STACK"; break;
            case KMAP_PAGETABLE: type_str = "PAGETABLE"; break;
            case KMAP_PMM:       type_str = "PMM"; break;
            case KMAP_SLAB:      type_str = "SLAB"; break;
            case KMAP_DEVICE:    type_str = "DEVICE"; break;
            case KMAP_DYNAMIC:   type_str = "DYNAMIC"; break;
            default:             type_str = "UNKNOWN"; break;
        }

        /* Pageable string */
        const char *pageable_str = (entry->pageable == KMAP_PAGEABLE) ? "yes" : "no";

        /* Monitor string */
        const char *monitor_str;
        switch (entry->monitor) {
            case KMAP_MONITOR_NONE:     monitor_str = "none"; break;
            case KMAP_MONITOR_READONLY: monitor_str = "readonly"; break;
            case KMAP_MONITOR_PRIVATE:  monitor_str = "private"; break;
            default:                    monitor_str = "unknown"; break;
        }

        klog_info("KMAP", "  0x%016lx 0x%016lx %-12s %-10s %-10s %s",
                  entry->virt_start, entry->virt_end,
                  type_str, pageable_str, monitor_str, entry->name);
    }

    spin_unlock_irqrestore(&kmap_lock, irq_flags);
}
