/* Emergence Kernel - Kernel Virtual Memory Mapping Management
 *
 * This header provides kernel-space memory region management for the
 * nested kernel architecture. Each region with the same mapping properties
 * has a kmap_t structure tracking virtual address range, physical backing,
 * permissions, pageable status, and monitor protection level.
 *
 * Key concepts:
 * - Non-pageable regions: Must always be resident (code, stacks, page tables)
 * - Pageable regions: Can be paged out on demand (data, heap, slab)
 * - Monitor protection: Regions protected by CR0.WP toggle for nested kernel
 */

#ifndef _KERNEL_KMAP_H
#define _KERNEL_KMAP_H

#include <stdint.h>
#include <stdbool.h>
#include "kernel/list.h"
#include "include/spinlock.h"

/* Page size definition (4KB standard pages) */
#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

/* Page table entry flags (from paging.h) */
#define X86_PTE_PRESENT    (1ULL << 0)   /* Present */
#define X86_PTE_WRITABLE   (1ULL << 1)   /* Read/Write */
#define X86_PTE_USER       (1ULL << 2)   /* User/Supervisor */
#define X86_PTE_NX         (1ULL << 63)  /* No-Execute */

/* Physical address mask - extract physical address from PTE */
#define X86_PTE_PHYS_MASK  0xFFFFFFF000ULL

/* Page alignment macros */
#define PAGE_ALIGN(addr)   (((addr) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define PAGE_ALIGNED(addr) (((addr) & (PAGE_SIZE - 1)) == 0)

/* ============================================================================
 * Kernel Memory Region Types
 * ============================================================================ */

/**
 * kmap_type_t - Kernel memory region classification
 *
 * Each region type has specific requirements for paging and monitor protection.
 */
typedef enum {
    KMAP_CODE,          /* Executable code (.text) - NON-PAGEABLE */
    KMAP_RODATA,        /* Read-only data - NON-PAGEABLE */
    KMAP_DATA,          /* Read-write data - PAGEABLE */
    KMAP_STACK,         /* Kernel stacks - NON-PAGEABLE */
    KMAP_PAGETABLE,     /* Page table pages - NON-PAGEABLE */
    KMAP_PMM,           /* PMM managed pages - PAGEABLE */
    KMAP_SLAB,          /* Slab allocator pages - PAGEABLE */
    KMAP_DEVICE,        /* Memory-mapped I/O - NON-PAGEABLE */
    KMAP_DYNAMIC,       /* kmalloc heap - PAGEABLE */
} kmap_type_t;

/**
 * kmap_pageable_t - Pageable status for kernel regions
 *
 * Determines whether a region can be paged out and handle page faults.
 */
typedef enum {
    KMAP_NONPAGEABLE,   /* Cannot page fault - always resident */
    KMAP_PAGEABLE,      /* Can be paged out/in on demand */
} kmap_pageable_t;

/**
 * kmap_monitor_t - Monitor protection level for nested kernel
 *
 * Defines how a region is protected by the monitor's CR0.WP toggle mechanism.
 */
typedef enum {
    KMAP_MONITOR_NONE,      /* No special monitor protection */
    KMAP_MONITOR_READONLY,  /* Read-only in NK mode (CR0.WP=0) */
    KMAP_MONITOR_PRIVATE,   /* Private to monitor (unmapped in OK mode) */
} kmap_monitor_t;

/* Forward declaration */
typedef struct kmap_s kmap_t;

/**
 * struct kmap_s - Kernel virtual memory mapping
 *
 * Each region with the same mapping properties has one kmap_s structure.
 * Tracks virtual address range, physical backing, permissions,
 * pageable status, and monitor protection.
 *
 * The kmap list is sorted by virtual address for efficient lookup.
 * Adjacent regions with identical properties can be merged.
 */
struct kmap_s {
    /* List management */
    struct list_head node;          /* Linkage in global kmap_list */

    /* Address range (page-aligned) */
    uint64_t virt_start;            /* Virtual start (inclusive) */
    uint64_t virt_end;              /* Virtual end (exclusive) */

    /* Physical backing */
    uint64_t phys_base;             /* Physical base (0 if pageable/alloc-on-demand) */

    /* Properties */
    kmap_type_t type;               /* Region type */
    kmap_pageable_t pageable;       /* Can this region be paged? */
    kmap_monitor_t monitor;         /* Monitor protection level */

    /* Page table flags */
    uint64_t flags;                 /* PTE flags (X86_PTE_*) */

    /* Reference counting */
    int32_t refcount;               /* For shared mappings */

    /* Statistics */
    uint64_t num_pages;             /* Number of pages in region */

    /* Debugging */
    char name[32];                  /* Human-readable name */
};

/* ============================================================================
 * Public API - Initialization
 * ============================================================================ */

/**
 * kmap_init - Initialize kmap subsystem and register boot mappings
 *
 * Must be called after PMM and slab allocator are initialized.
 * Registers all boot mappings created during kernel initialization.
 */
void kmap_init(void);

/* ============================================================================
 * Public API - Core Operations
 * ============================================================================ */

/**
 * kmap_create - Create a new kernel mapping
 * @virt_start: Virtual start address (must be page-aligned)
 * @virt_end: Virtual end address (must be page-aligned)
 * @phys_base: Physical base address (0 for pageable/alloc-on-demand)
 * @flags: PTE flags (X86_PTE_PRESENT | X86_PTE_WRITABLE, etc.)
 * @type: Region type (KMAP_CODE, KMAP_DATA, etc.)
 * @pageable: Pageable status (KMAP_PAGEABLE or KMAP_NONPAGEABLE)
 * @monitor: Monitor protection level
 * @name: Human-readable name for debugging
 *
 * Returns: Pointer to new kmap_t, or NULL on failure
 *
 * Creates a new kernel mapping and adds it to the global kmap list.
 * The list is kept sorted by virtual address.
 */
kmap_t *kmap_create(uint64_t virt_start, uint64_t virt_end,
                    uint64_t phys_base, uint64_t flags,
                    kmap_type_t type, kmap_pageable_t pageable,
                    kmap_monitor_t monitor, const char *name);

/**
 * kmap_destroy - Destroy a kernel mapping
 * @mapping: Mapping to destroy
 *
 * Returns: 0 on success, negative error code on failure
 *
 * Removes the mapping from the list and frees associated resources.
 * Returns error if refcount is non-zero.
 */
int kmap_destroy(kmap_t *mapping);

/**
 * kmap_get - Increment reference count
 * @mapping: Mapping to reference
 */
void kmap_get(kmap_t *mapping);

/**
 * kmap_put - Decrement reference count and free if zero
 * @mapping: Mapping to release
 */
void kmap_put(kmap_t *mapping);

/**
 * kmap_modify_flags - Modify mapping flags
 * @mapping: Mapping to modify
 * @new_flags: New PTE flags
 *
 * Returns: 0 on success, negative error code on failure
 *
 * Updates the PTE flags for all pages in the mapping.
 * Invalidates TLB entries as needed.
 */
int kmap_modify_flags(kmap_t *mapping, uint64_t new_flags);

/**
 * kmap_split - Split a mapping at address
 * @mapping: Mapping to split
 * @split_addr: Address to split at (must be page-aligned)
 *
 * Returns: 0 on success, negative error code on failure
 *
 * Splits the mapping into two at the specified address.
 * Original mapping covers [start, split_addr), new mapping covers [split_addr, end).
 */
int kmap_split(kmap_t *mapping, uint64_t split_addr);

/**
 * kmap_merge - Merge adjacent compatible mappings
 * @m1: First mapping
 * @m2: Second mapping (must be immediately after m1)
 *
 * Returns: 0 on success, negative error code on failure
 *
 * Merges two adjacent mappings if they have identical properties.
 * The second mapping is removed from the list.
 */
int kmap_merge(kmap_t *m1, kmap_t *m2);

/* ============================================================================
 * Public API - Lookup
 * ============================================================================ */

/**
 * kmap_lookup - Find mapping containing address
 * @addr: Virtual address to look up
 *
 * Returns: Pointer to kmap_t, or NULL if not found
 *
 * Searches the kmap list for a region containing the specified address.
 */
kmap_t *kmap_lookup(uint64_t addr);

/**
 * kmap_lookup_range - Find mapping overlapping range
 * @start: Range start
 * @end: Range end (exclusive)
 *
 * Returns: Pointer to kmap_t, or NULL if not found
 *
 * Finds any mapping that overlaps the specified range.
 */
kmap_t *kmap_lookup_range(uint64_t start, uint64_t end);

/**
 * kmap_is_pageable - Query pageable status of address
 * @addr: Virtual address
 *
 * Returns: true if address is in a pageable region
 */
bool kmap_is_pageable(uint64_t addr);

/**
 * kmap_get_monitor_protection - Query monitor protection level
 * @addr: Virtual address
 *
 * Returns: Monitor protection level for the address
 */
kmap_monitor_t kmap_get_monitor_protection(uint64_t addr);

/* ============================================================================
 * Public API - Page Table Operations
 * ============================================================================ */

/**
 * kmap_map_pages - Map pages for a kmap region
 * @mapping: Mapping to create page tables for
 *
 * Returns: 0 on success, negative error code on failure
 *
 * Creates page table entries for all pages in the mapping.
 * Allocates physical pages if phys_base is 0 (pageable regions).
 */
int kmap_map_pages(kmap_t *mapping);

/**
 * kmap_unmap_pages - Unmap pages for a kmap region
 * @mapping: Mapping to remove page tables for
 *
 * Returns: 0 on success, negative error code on failure
 *
 * Removes page table entries for all pages in the mapping.
 * Returns physical pages to PMM if they were allocated.
 */
int kmap_unmap_pages(kmap_t *mapping);

/* ============================================================================
 * Public API - Demand Paging
 * ============================================================================ */

/**
 * kmap_handle_page_fault - Handle page fault in pageable region
 * @fault_addr: Faulting virtual address from CR2
 * @error_code: Page fault error code
 *
 * Returns: 0 on success, negative error code on failure
 *
 * Called from page_fault_handler() to handle faults in pageable regions.
 * Allocates a physical page, maps it, and returns.
 * Returns error if fault is in non-pageable region (shouldn't happen).
 */
int kmap_handle_page_fault(uint64_t fault_addr, uint64_t error_code);

/* ============================================================================
 * Public API - Debugging
 * ============================================================================ */

/**
 * kmap_dump - Dump all kmap entries to serial console
 *
 * Prints each mapping's address range, type, pageable status,
 * and monitor protection level.
 */
void kmap_dump(void);

#endif /* _KERNEL_KMAP_H */
