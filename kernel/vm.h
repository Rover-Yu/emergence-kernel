/* Emergence Kernel - Virtual Memory Management
 *
 * This header provides process address space management for user processes.
 * Each process has its own address space with isolated page tables.
 */

#ifndef _KERNEL_VM_H
#define _KERNEL_VM_H

#include <stdint.h>
#include <stddef.h>
#include "kernel/list.h"
#include "include/spinlock.h"

/* Forward declarations */
typedef struct address_space address_space_t;
typedef struct vm_region vm_region_t;

/* Page table entry flags */
#define PT_PRESENT     (1ULL << 0)   /* Present */
#define PT_WRITE       (1ULL << 1)   /* Read/Write */
#define PT_USER        (1ULL << 2)   /* User (CPL 3) */
#define PT_WRITETHROUGH (1ULL << 3)  /* Write-Through */
#define PT_NOCACHE     (1ULL << 4)   /* Cache Disable */
#define PT_ACCESSED    (1ULL << 5)   /* Accessed */
#define PT_DIRTY       (1ULL << 6)   /* Dirty */
#define PT_GLOBAL      (1ULL << 8)   /* Global (shared across address spaces) */
#define PT_NX          (1ULL << 63)  /* No-execute */

/* Memory region types */
typedef enum {
    VM_REGION_NONE,       /* Uninitialized/unused */
    VM_REGION_CODE,       /* Executable code */
    VM_REGION_DATA,       /* Read-write data */
    VM_REGION_RODATA,     /* Read-only data */
    VM_REGION_STACK,      /* User stack */
    VM_REGION_HEAP,       /* Heap (brk managed) */
    VM_REGION_MMAP,       /* Memory-mapped region */
    VM_REGION_SHARED,     /* Shared memory region */
} vm_region_type_t;

/* Memory region permissions */
typedef enum {
    VM_PERM_NONE    = 0,       /* No access */
    VM_PERM_READ    = (1 << 0), /* Readable */
    VM_PERM_WRITE   = (1 << 1), /* Writable */
    VM_PERM_EXEC    = (1 << 2), /* Executable */
} vm_perm_t;

/**
 * struct vm_region - A contiguous memory region in an address space
 * @node: List linkage for address space's region list
 * @type: Region type (code, data, stack, etc.)
 * @start: Virtual start address (page-aligned)
 * @end: Virtual end address (page-aligned, exclusive)
 * @phys_base: Physical base address (for direct mapping)
 * @flags: Page table flags (PT_USER, PT_WRITE, etc.)
 * @perm: Permission bits (VM_PERM_*)
 * @name: Region name for debugging
 *
 * Represents a contiguous region of virtual memory in a process's
 * address space. Each region has consistent permissions and is backed
 * by physical pages.
 */
struct vm_region {
    struct list_head node;      /* Linkage for address_space.regions */
    vm_region_type_t type;      /* Region type */
    uint64_t start;             /* Virtual start address (inclusive) */
    uint64_t end;               /* Virtual end address (exclusive) */
    uint64_t phys_base;         /* Physical base address */
    uint64_t flags;             /* Page table entry flags */
    uint32_t perm;              /* Permission bits */
    char name[32];              /* Region name for debugging */
};

/**
 * struct address_space - Process virtual address space
 * @refcount: Reference count for shared address spaces
 * @pml4: Physical address of PML4 page table
 * @regions: List of memory regions in this address space
 * @region_count: Number of regions
 * @lock: Spinlock protecting region list modifications
 * @brk: Current heap break (for brk() syscall)
 * @start_brk: Initial heap break
 *
 * Represents the complete virtual address space of a process.
 * Contains the page table root and list of all memory regions.
 */
struct address_space {
    int refcount;               /* Reference count for sharing */
    uint64_t pml4_phys;         /* Physical address of PML4 */
    uint64_t *pml4;             /* Virtual address of PML4 (kernel mapping) */
    struct list_head regions;   /* List of vm_region structures */
    int region_count;           /* Number of regions */
    spinlock_t lock;            /* Spinlock for regions list */
    uint64_t brk;               /* Current heap break */
    uint64_t start_brk;         /* Initial heap break */
};

/* User memory layout constants */
#define USER_STACK_BASE   0x7ffffffff000ULL  /* Top of user space (minus guard page) */
#define USER_STACK_SIZE   (1024 * 1024)      /* 1 MB default stack */
#define USER_HEAP_BASE    0x1000000          /* 16 MB heap start */
#define USER_HEAP_MAX     0x40000000         /* 1 GB heap limit */
#define USER_MMAP_BASE    0x40000000         /* 1 GB mmap start */
#define USER_MMAP_END     0x80000000         /* 2 GB mmap end */

/* Address space flags for vm_create_address_space */
#define AS_SHARE_KERNEL   (1 << 0)  /* Share kernel page mappings (recommended) */

/**
 * vm_init - Initialize virtual memory subsystem
 *
 * Sets up the kernel's master page table and prepares for
 * address space creation. Must be called before any vm_* operations.
 */
void vm_init(void);

/**
 * vm_create_address_space - Create a new address space
 * @flags: Creation flags (AS_SHARE_KERNEL, etc.)
 *
 * Returns: Pointer to new address_space, or NULL on failure
 *
 * Creates a new address space with its own page tables.
 * If AS_SHARE_KERNEL is set, kernel mappings are copied from
 * the master kernel page table.
 */
address_space_t *vm_create_address_space(int flags);

/**
 * vm_clone_address_space - Clone an existing address space
 * @src: Source address space to clone
 *
 * Returns: Pointer to cloned address_space, or NULL on failure
 *
 * Creates a copy of an address space for fork().
 * Copies all memory regions and creates new page tables.
 * Used by fork() to create child process address space.
 */
address_space_t *vm_clone_address_space(address_space_t *src);

/**
 * vm_destroy_address_space - Destroy an address space
 * @as: Address space to destroy
 *
 * Frees all page tables and memory regions associated with
 * the address space. The address_space structure itself is freed.
 */
void vm_destroy_address_space(address_space_t *as);

/**
 * vm_get_address_space - Increment reference count
 * @as: Address space
 *
 * Increments the reference count for shared address spaces.
 * Must be called when a new process references an existing space.
 */
void vm_get_address_space(address_space_t *as);

/**
 * vm_put_address_space - Decrement reference count
 * @as: Address space
 *
 * Decrements reference count and destroys address space if count reaches 0.
 * Must be called when a process stops using an address space.
 */
void vm_put_address_space(address_space_t *as);

/**
 * vm_switch_address_space - Switch to a different address space
 * @as: Address space to activate
 *
 * Loads the address space's page table into CR3.
 * Causes all subsequent memory accesses to use the new page tables.
 * TLB flush occurs as a side effect of CR3 write.
 */
void vm_switch_address_space(address_space_t *as);

/**
 * vm_map_region - Map a memory region into an address space
 * @as: Address space
 * @start: Virtual start address (must be page-aligned)
 * @phys: Physical start address (must be page-aligned)
 * @size: Size in bytes (must be multiple of PAGE_SIZE)
 * @flags: Page table flags (PT_USER | PT_WRITE | PT_EXEC, etc.)
 * @type: Region type for tracking
 * @name: Region name for debugging
 *
 * Returns: 0 on success, negative error code on failure
 *
 * Maps a contiguous region of physical memory into the address space.
 * Creates page table entries as needed. The region is tracked in the
 * address space's region list.
 */
int vm_map_region(address_space_t *as,
                  uint64_t start,
                  uint64_t phys,
                  uint64_t size,
                  uint64_t flags,
                  vm_region_type_t type,
                  const char *name);

/**
 * vm_unmap_region - Unmap a memory region from an address space
 * @as: Address space
 * @start: Virtual start address
 * @size: Size in bytes
 *
 * Returns: 0 on success, negative error code on failure
 *
 * Unmaps the specified region and removes it from the region list.
 * Physical pages are returned to the PMM.
 */
int vm_unmap_region(address_space_t *as, uint64_t start, uint64_t size);

/**
 * vm_find_region - Find a region containing an address
 * @as: Address space
 * @addr: Virtual address to look up
 *
 * Returns: Pointer to vm_region, or NULL if not found
 *
 * Searches the address space's region list for a region
 * that contains the specified address.
 */
vm_region_t *vm_find_region(address_space_t *as, uint64_t addr);

/**
 * vm_protect_region - Change protection of a memory region
 * @as: Address space
 * @start: Virtual start address
 * @size: Size in bytes
 * @flags: New page table flags
 *
 * Returns: 0 on success, negative error code on failure
 *
 * Updates page table entries for the specified range with new
 * protection flags (e.g., make code read-only with PT_NX).
 */
int vm_protect_region(address_space_t *as,
                      uint64_t start,
                      uint64_t size,
                      uint64_t flags);

/**
 * vm_alloc_user_pages - Allocate and map physical pages for user memory
 * @as: Address space
 * @start: Virtual start address (must be page-aligned)
 * @size: Size in bytes (must be multiple of PAGE_SIZE)
 * @flags: Page table flags
 *
 * Returns: 0 on success, negative error code on failure
 *
 * Allocates physical pages from PMM and maps them into the
 * address space at the specified virtual address.
 */
int vm_alloc_user_pages(address_space_t *as,
                        uint64_t start,
                        uint64_t size,
                        uint64_t flags);

#endif /* _KERNEL_VM_H */
