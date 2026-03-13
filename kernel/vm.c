/* Emergence Kernel - Virtual Memory Management Implementation
 *
 * Provides process address space management with isolated page tables.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "kernel/vm.h"
#include "kernel/pmm.h"
#include "kernel/klog.h"
#include "kernel/slab.h"
#include "include/string.h"

/* External: master kernel page table from boot.S */
extern uint64_t boot_pml4[];

/* Master kernel page table - identity mapped in all address spaces */
static uint64_t *master_kernel_pml4 = NULL;

/* Slab cache for address_space structures */
static slab_cache_t address_space_cache_struct;
static slab_cache_t *address_space_cache = &address_space_cache_struct;

/* Slab cache for vm_region structures */
static slab_cache_t vm_region_cache_struct;
static slab_cache_t *vm_region_cache = &vm_region_cache_struct;

/**
 * vm_init - Initialize virtual memory subsystem
 */
void vm_init(void)
{
    int ret;

    klog_info("VM", "Initializing virtual memory subsystem");

    /* Store reference to master kernel page table */
    master_kernel_pml4 = boot_pml4;

    /* Initialize slab cache for address_space structures */
    ret = slab_cache_create(address_space_cache, sizeof(address_space_t));
    if (ret != 0) {
        klog_error("VM", "Failed to create address_space slab cache");
        return;
    }

    /* Initialize slab cache for vm_region structures */
    ret = slab_cache_create(vm_region_cache, sizeof(vm_region_t));
    if (ret != 0) {
        klog_error("VM", "Failed to create vm_region slab cache");
        return;
    }

    klog_info("VM", "VM subsystem initialized (PML4 at %p)", master_kernel_pml4);
}

/**
 * vm_create_address_space - Create a new address space
 * @flags: Creation flags
 *
 * Returns: Pointer to new address_space, or NULL on failure
 */
address_space_t *vm_create_address_space(int flags)
{
    address_space_t *as;
    uint64_t *new_pml4;
    void *pml4_phys;

    klog_debug("VM", "Creating new address space (flags=%x)", flags);

    /* Allocate address_space structure */
    as = slab_alloc(address_space_cache);
    if (as == NULL) {
        klog_error("VM", "Failed to allocate address_space structure");
        return NULL;
    }

    /* Clear structure */
    memset(as, 0, sizeof(address_space_t));

    /* Allocate new PML4 page */
    new_pml4 = pmm_alloc(0);  /* Allocate 1 page */
    if (new_pml4 == NULL) {
        klog_error("VM", "Failed to allocate PML4 page");
        slab_free(address_space_cache, as);
        return NULL;
    }

    /* Clear PML4 */
    memset(new_pml4, 0, PAGE_SIZE);

    /* Store physical and virtual addresses */
    pml4_phys = new_pml4;  /* Physical address (identity mapped for now) */
    as->pml4 = new_pml4;
    as->pml4_phys = (uint64_t)(uintptr_t)pml4_phys;

    /* Initialize region list */
    list_init(&as->regions);
    as->region_count = 0;
    as->lock = 0;

    /* Initialize heap */
    as->start_brk = USER_HEAP_BASE;
    as->brk = USER_HEAP_BASE;

    /* Set reference count */
    as->refcount = 1;

    /* Share kernel mappings if requested */
    if (flags & AS_SHARE_KERNEL) {
        /* Copy kernel PML4 entries (indices 256-511)
         * User space uses indices 0-255, kernel uses 256-511 */
        for (int i = 256; i < 512; i++) {
            new_pml4[i] = master_kernel_pml4[i];
        }

        klog_debug("VM", "Copied kernel mappings to new address space");
    }

    klog_info("VM", "Created address space (PML4=%p, phys=%p)",
              as->pml4, as->pml4_phys);

    return as;
}

/**
 * vm_destroy_address_space - Destroy an address space
 * @as: Address space to destroy
 */
void vm_destroy_address_space(address_space_t *as)
{
    struct list_head *pos, *n;

    if (as == NULL) {
        return;
    }

    klog_debug("VM", "Destroying address space (PML4=%p)", as->pml4);

    /* Free all memory regions */
    list_for_each_safe(pos, n, &as->regions) {
        vm_region_t *region = list_entry(pos, vm_region_t, node);
        list_remove(pos);
        slab_free(vm_region_cache, region);
    }

    /* Free PML4 page table */
    pmm_free(as->pml4, 0);

    /* Free address_space structure */
    slab_free(address_space_cache, as);
}

/**
 * vm_get_address_space - Increment reference count
 * @as: Address space
 */
void vm_get_address_space(address_space_t *as)
{
    if (as != NULL) {
        as->refcount++;
    }
}

/**
 * vm_put_address_space - Decrement reference count
 * @as: Address space
 */
void vm_put_address_space(address_space_t *as)
{
    if (as != NULL) {
        as->refcount--;
        if (as->refcount <= 0) {
            vm_destroy_address_space(as);
        }
    }
}

/**
 * vm_switch_address_space - Switch to a different address space
 * @as: Address space to activate
 */
void vm_switch_address_space(address_space_t *as)
{
    uint64_t cr3;

    if (as == NULL) {
        klog_warn("VM", "Attempted to switch to NULL address space");
        return;
    }

    klog_debug("VM", "Switching to address space (PML4=%p)", as->pml4);

    /* Load new CR3 value */
    cr3 = as->pml4_phys;

    /* Ensure PCID bits are clear (no PCID support for now) */
    cr3 &= ~0xFFF;

    __asm__ volatile ("mov %0, %%cr3" : : "r"(cr3) : "memory");

    klog_debug("VM", "CR3 switched to %p", cr3);
}

/**
 * vm_map_region - Map a memory region into an address space
 * @as: Address space
 * @start: Virtual start address
 * @phys: Physical start address
 * @size: Size in bytes
 * @flags: Page table flags
 * @type: Region type
 * @name: Region name
 *
 * Returns: 0 on success, negative error code on failure
 */
int vm_map_region(address_space_t *as,
                  uint64_t start,
                  uint64_t phys,
                  uint64_t size,
                  uint64_t flags,
                  vm_region_type_t type,
                  const char *name)
{
    vm_region_t *region;
    uint64_t virt;
    uint64_t num_pages;

    klog_debug("VM", "Mapping region %s: virt=%p phys=%p size=%p",
               name, start, phys, size);

    /* Validate alignment */
    if ((start & (PAGE_SIZE - 1)) || (phys & (PAGE_SIZE - 1))) {
        klog_error("VM", " Misaligned addresses (virt=%p, phys=%p)", start, phys);
        return -22;  /* EINVAL */
    }

    if ((size & (PAGE_SIZE - 1)) || size == 0) {
        klog_error("VM", "Invalid size: %p", size);
        return -22;
    }

    /* Allocate and initialize region structure */
    region = slab_alloc(vm_region_cache);
    if (region == NULL) {
        klog_error("VM", "Failed to allocate vm_region");
        return -12;  /* ENOMEM */
    }

    region->start = start;
    region->end = start + size;
    region->phys_base = phys;
    region->flags = flags;
    region->type = type;
    region->perm = 0;
    if (flags & PT_WRITE) region->perm |= VM_PERM_WRITE;
    if (flags & PT_NX) region->perm &= ~VM_PERM_EXEC;
    else region->perm |= VM_PERM_EXEC;
    region->perm |= VM_PERM_READ;

    /* Copy name */
    if (name != NULL) {
        strncpy(region->name, name, sizeof(region->name) - 1);
        region->name[sizeof(region->name) - 1] = '\0';
    } else {
        strcpy(region->name, "anonymous");
    }

    /* Add to region list */
    list_push_back(&as->regions, &region->node);
    as->region_count++;

    /* Map pages into page tables */
    num_pages = size / PAGE_SIZE;
    for (uint64_t i = 0; i < num_pages; i++) {
        virt = start + (i * PAGE_SIZE);
        uint64_t phys_addr = phys + (i * PAGE_SIZE);

        /* TODO: Implement full page table walk
         * For now, this is a stub */
        (void)phys_addr;
        (void)virt;
    }

    klog_debug("VM", "Region mapped successfully (%p - %p)", start, start + size);

    return 0;
}

/**
 * vm_find_region - Find a region containing an address
 * @as: Address space
 * @addr: Virtual address
 *
 * Returns: Pointer to vm_region, or NULL if not found
 */
vm_region_t *vm_find_region(address_space_t *as, uint64_t addr)
{
    struct list_head *pos;

    list_for_each(pos, &as->regions) {
        vm_region_t *region = list_entry(pos, vm_region_t, node);
        if (addr >= region->start && addr < region->end) {
            return region;
        }
    }

    return NULL;
}

/**
 * vm_clone_address_space - Clone an existing address space
 * @src: Source address space
 *
 * Returns: Pointer to cloned address_space, or NULL on failure
 */
address_space_t *vm_clone_address_space(address_space_t *src)
{
    address_space_t *as;
    struct list_head *pos;

    klog_debug("VM", "Cloning address space (PML4=%p)", src->pml4);

    /* Create new address space */
    as = vm_create_address_space(AS_SHARE_KERNEL);
    if (as == NULL) {
        klog_error("VM", "Failed to create address space for clone");
        return NULL;
    }

    /* Copy heap settings */
    as->start_brk = src->start_brk;
    as->brk = src->brk;

    /* Clone all regions */
    list_for_each(pos, &src->regions) {
        vm_region_t *region = list_entry(pos, vm_region_t, node);
        vm_region_t *new_region;

        /* Allocate new region structure */
        new_region = slab_alloc(vm_region_cache);
        if (new_region == NULL) {
            klog_error("VM", "Failed to allocate region for clone");
            vm_destroy_address_space(as);
            return NULL;
        }

        /* Copy region data */
        memcpy(new_region, region, sizeof(vm_region_t));

        /* TODO: For fork(), we need CoW (copy-on-write)
         * For now, just copy the metadata */
        new_region->phys_base = region->phys_base;  /* Shared physical for now */

        /* Add to new address space */
        list_push_back(&as->regions, &new_region->node);
        as->region_count++;
    }

    klog_info("VM", "Cloned address space (%d regions)", as->region_count);

    return as;
}
