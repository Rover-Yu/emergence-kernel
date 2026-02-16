/* Emergence Kernel - Page Control Data (PCD) Implementation */

#include "kernel/pcd.h"
#include "kernel/pmm.h"
#include "arch/x86_64/smp.h"
#include "kernel/klog.h"

/* External symbols for kernel region */
extern char _kernel_start[];
extern char _kernel_end[];

/* External symbols for nested kernel stacks (from boot.S .bss section) */
extern uint8_t nk_boot_stack_bottom[];   /* Nested kernel boot stack in boot.S */
extern uint8_t nk_boot_stack_top[];
extern uint8_t nk_trampoline_stack_bottom[]; /* AP trampoline stack area in boot.S */
extern uint8_t nk_trampoline_stack_end[];

/* Note: ap_stacks in smp.c is static - it's covered by default NK_NORMAL initialization */

/* PCD state - global instance */
static pcd_state_t pcd_state;

/* Internal PCD function for monitor access */
void _pcd_set_type_internal(uint64_t phys_addr, uint8_t type) {
    /* Convert physical address to page index */
    uint64_t page_index = (phys_addr >> PAGE_SHIFT) - pcd_state.base_page;

    /* Check bounds */
    if (page_index >= pcd_state.max_pages) {
        return;
    }

    /* Set the page type */
    pcd_state.pages[page_index].type = type;
}

/* ============================================================================
 * Internal Helpers
 * ============================================================================ */

/* Helper: Convert physical address to page number */
static inline uint64_t phys_to_page(uint64_t phys_addr) {
    return phys_addr >> PAGE_SHIFT;
}

/* Helper: Convert page number to physical address */
static inline uint64_t page_to_phys(uint64_t page_num) {
    return page_num << PAGE_SHIFT;
}

/* Helper: Get PCD index for a physical address */
static inline uint64_t pcd_get_index(uint64_t phys_addr) {
    uint64_t page_num = phys_to_page(phys_addr);
    if (page_num < pcd_state.base_page) {
        return 0;  /* Before managed region */
    }
    uint64_t index = page_num - pcd_state.base_page;
    if (index >= pcd_state.max_pages) {
        return pcd_state.max_pages - 1;  /* Clamp to end */
    }
    return index;
}

/* Helper: Check if physical address is managed by PCD */
static inline bool pcd_is_managed(uint64_t phys_addr) {
    uint64_t page_num = phys_to_page(phys_addr);
    return (page_num >= pcd_state.base_page) &&
           (page_num < pcd_state.base_page + pcd_state.max_pages);
}

/* ============================================================================
 * Public API Implementation
 * ============================================================================ */

/**
 * pcd_init - Initialize Page Control Data system
 *
 * Allocates PCD array from PMM and initializes all pages as NK_NORMAL.
 * Must be called after PMM is initialized but before allocating pages
 * for outer kernel use.
 */
void pcd_init(void) {
    klog_debug("PCD", "Initializing Page Control Data system");

    /* Initialize lock */
    spin_lock_init(&pcd_state.lock);
    pcd_state.initialized = false;

    /* Determine memory size from PMM */
    uint64_t total_pages = pmm_get_total_pages();
    if (total_pages == 0) {
        klog_error("PCD", "PMM reports zero pages");
        return;
    }

    /* Calculate PCD array size needed */
    uint64_t pcd_array_size = total_pages * sizeof(pcd_t);
    uint8_t pcd_order = 0;
    uint64_t order_size = PAGE_SIZE;

    /* Find order needed for PCD array */
    while (order_size < pcd_array_size && pcd_order < MAX_ORDER) {
        pcd_order++;
        order_size <<= 1;
    }

    if (pcd_order > MAX_ORDER) {
        klog_error("PCD", "Memory too large for PCD tracking");
        return;
    }

    klog_debug("PCD", "Allocating %lX bytes for PCD array (order %d)",
              pcd_array_size, pcd_order);

    /* Allocate PCD array from PMM (chicken-and-egg solved!) */
    irq_flags_t flags = spin_lock_irqsave(&pcd_state.lock);

    pcd_state.pages = (pcd_t *)pmm_alloc(pcd_order);
    if (!pcd_state.pages) {
        klog_error("PCD", "Failed to allocate PCD array");
        spin_unlock_irqrestore(&pcd_state.lock, flags);
        return;
    }

    /* Initialize PCD state */
    pcd_state.max_pages = total_pages;
    pcd_state.base_page = 0;

    /* Initialize all PCD entries as NK_NORMAL (monitor-owned by default) */
    for (uint64_t i = 0; i < total_pages; i++) {
        pcd_state.pages[i].type = PCD_TYPE_NK_NORMAL;
        pcd_state.pages[i].flags = 0;
        pcd_state.pages[i].reserved = 0;
    }

    pcd_state.initialized = true;
    spin_unlock_irqrestore(&pcd_state.lock, flags);

    klog_info("PCD", "Managing %lX pages (%lX bytes)",
             total_pages, total_pages * PAGE_SIZE);

    /* Mark kernel code region as NK_NORMAL */
    uint64_t kernel_start = (uint64_t)_kernel_start;
    uint64_t kernel_end = (uint64_t)_kernel_end;
    pcd_mark_region(kernel_start, kernel_end - kernel_start, PCD_TYPE_NK_NORMAL);

    /* Mark nested kernel stacks as NK_NORMAL (monitor-owned, read-only for outer kernel) */
    klog_debug("PCD", "Marking nested kernel stacks as NK_NORMAL");

    /* Nested kernel boot stack (16 KiB in boot.S) */
    uint64_t nk_boot_stack_start = (uint64_t)nk_boot_stack_bottom;
    uint64_t nk_boot_stack_size = (uint64_t)nk_boot_stack_top - (uint64_t)nk_boot_stack_bottom;
    pcd_mark_region(nk_boot_stack_start, nk_boot_stack_size, PCD_TYPE_NK_NORMAL);

    /* AP trampoline stack area from boot.S (16 KiB) */
    uint64_t trampoline_stack_start = (uint64_t)nk_trampoline_stack_bottom;
    uint64_t trampoline_stack_size = (uint64_t)nk_trampoline_stack_end - (uint64_t)nk_trampoline_stack_bottom;
    pcd_mark_region(trampoline_stack_start, trampoline_stack_size, PCD_TYPE_NK_NORMAL);

    /* Mark outer kernel CPU stacks as OK_NORMAL for outer kernel use */
    klog_debug("PCD", "Marking outer kernel CPU stacks as OK_NORMAL");

    /* ok_cpu_stacks is defined in smp.c - we need to get its address */
    extern uint8_t ok_cpu_stacks[];
    uint64_t ok_stacks_start = (uint64_t)ok_cpu_stacks;
    /* ok_cpu_stacks[SMP_MAX_CPUS][CPU_STACK_SIZE] = 4 * 16384 = 64KB */
    uint64_t ok_stacks_size = SMP_MAX_CPUS * CPU_STACK_SIZE;
    pcd_mark_region(ok_stacks_start, ok_stacks_size, PCD_TYPE_OK_NORMAL);

    klog_info("PCD", "Initialized successfully");
}

/**
 * pcd_set_type - Set page type for a physical page
 * @phys_addr: Physical address of page
 * @type: New page type (PCD_TYPE_*)
 *
 * Returns: 0 on success, -1 on error
 */
int pcd_set_type(uint64_t phys_addr, uint8_t type) {
    if (!pcd_state.initialized) {
        klog_error("PCD", "pcd_set_type called before init");
        return -1;
    }

    /* Validate type */
    if (type < PCD_TYPE_MIN || type > PCD_TYPE_MAX) {
        klog_error("PCD", "Invalid page type: %d", type);
        return -1;
    }

    /* Align to page boundary */
    phys_addr = phys_addr & ~(PAGE_SIZE - 1);

    irq_flags_t flags = spin_lock_irqsave(&pcd_state.lock);

    if (!pcd_is_managed(phys_addr)) {
        spin_unlock_irqrestore(&pcd_state.lock, flags);
        klog_warn("PCD", "Address not managed: %p", (void *)phys_addr);
        return -1;
    }

    uint64_t index = pcd_get_index(phys_addr);
    pcd_state.pages[index].type = type;

    spin_unlock_irqrestore(&pcd_state.lock, flags);
    return 0;
}

/**
 * pcd_get_type - Get page type for a physical page
 * @phys_addr: Physical address of page
 *
 * Returns: Page type (PCD_TYPE_*), or PCD_TYPE_NK_NORMAL if not managed
 */
uint8_t pcd_get_type(uint64_t phys_addr) {
    uint8_t type;

    if (!pcd_state.initialized) {
        return PCD_TYPE_NK_NORMAL;  /* Default to monitor-owned */
    }

    /* Align to page boundary */
    phys_addr = phys_addr & ~(PAGE_SIZE - 1);

    irq_flags_t flags = spin_lock_irqsave(&pcd_state.lock);

    if (!pcd_is_managed(phys_addr)) {
        type = PCD_TYPE_NK_NORMAL;  /* Default for unmanaged pages */
    } else {
        uint64_t index = pcd_get_index(phys_addr);
        type = pcd_state.pages[index].type;
    }

    spin_unlock_irqrestore(&pcd_state.lock, flags);

    return type;
}

/**
 * pcd_mark_region - Mark a memory region with a specific type
 * @base: Physical base address of region
 * @size: Size of region in bytes
 * @type: Page type to assign (PCD_TYPE_*)
 *
 * Returns: Number of pages marked
 */
int pcd_mark_region(uint64_t base, uint64_t size, uint8_t type) {
    uint64_t addr;
    uint64_t end;
    irq_flags_t flags;
    int count = 0;

    if (!pcd_state.initialized) {
        return 0;
    }

    /* Align to page boundaries */
    addr = (base + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    end = (base + size) & ~(PAGE_SIZE - 1);

    if (addr >= end) {
        return 0;
    }

    flags = spin_lock_irqsave(&pcd_state.lock);

    /* Mark each page in the region */
    for (uint64_t page = addr; page < end; page += PAGE_SIZE) {
        if (pcd_is_managed(page)) {
            uint64_t index = pcd_get_index(page);
            pcd_state.pages[index].type = type;
            count++;
        }
    }

    spin_unlock_irqrestore(&pcd_state.lock, flags);
    return count;
}

/**
 * pcd_is_initialized - Check if PCD system is initialized
 *
 * Returns: true if PCD is ready
 */
bool pcd_is_initialized(void) {
    return pcd_state.initialized;
}

/**
 * pcd_get_max_pages - Get maximum number of pages managed
 *
 * Returns: Number of pages managed by PCD
 */
uint64_t pcd_get_max_pages(void) {
    return pcd_state.max_pages;
}

/**
 * pcd_dump_stats - Dump PCD statistics for debugging
 *
 * Prints count of each page type for diagnostic purposes.
 */
void pcd_dump_stats(void) {
    uint64_t counts[4] = {0, 0, 0, 0};

    if (!pcd_state.initialized) {
        klog_info("PCD", "Not initialized");
        return;
    }

    irq_flags_t flags = spin_lock_irqsave(&pcd_state.lock);

    /* Count pages by type */
    for (uint64_t i = 0; i < pcd_state.max_pages; i++) {
        uint8_t type = pcd_state.pages[i].type;
        if (type <= PCD_TYPE_MAX) {
            counts[type]++;
        }
    }

    spin_unlock_irqrestore(&pcd_state.lock, flags);

    /* Print statistics */
    klog_info("PCD", "Page type statistics:");
    klog_info("PCD", "  OK_NORMAL:  %lX pages", counts[0]);
    klog_info("PCD", "  NK_NORMAL:  %lX pages", counts[1]);
    klog_info("PCD", "  NK_PGTABLE: %lX pages", counts[2]);
    klog_info("PCD", "  NK_IO:      %lX pages", counts[3]);
}
