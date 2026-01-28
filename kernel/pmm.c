/* Emergence Kernel - Physical Memory Manager (Buddy System Implementation) */

#include <stdint.h>
#include "kernel/pmm.h"
#include "kernel/multiboot2.h"
#include "include/spinlock.h"
#include "arch/x86_64/serial.h"

/* External symbols for kernel region reservation */
extern char _kernel_start[];
extern char _kernel_end[];
extern char _end[];

/* External serial functions */
extern void serial_puts(const char *str);
extern void serial_putc(char c);

/* PMM state - global instance */
static pmm_state_t pmm_state;

/* Helper: Convert number to hex string */
static void put_hex(uint64_t value) {
    const char hex_chars[] = "0123456789ABCDEF";
    char buf[17];
    int i;

    /* Handle zero */
    if (value == 0) {
        serial_puts("0");
        return;
    }

    /* Convert to hex (reverse) */
    for (i = 15; i >= 0; i--) {
        buf[i] = hex_chars[value & 0xF];
        value >>= 4;
        if (value == 0) break;
    }

    /* Find first non-zero */
    while (i < 15 && buf[i] == '0') i++;

    /* Print */
    while (i <= 15) {
        serial_putc(buf[i++]);
    }
}

/* Helper: Find block by physical address in allocated list */
static block_info_t *find_allocated_block(uint64_t addr) {
    block_info_t *block;
    struct list_head *pos;

    list_for_each(pos, &pmm_state.allocated_blocks) {
        block = list_entry(pos, block_info_t, list);
        if (block->base_addr == addr) {
            return block;
        }
    }
    return 0;
}

/* Get buddy address for a block */
static uint64_t get_buddy_addr(uint64_t addr, uint8_t order) {
    uint64_t size = PAGE_SIZE << order;
    return addr ^ size;
}

/* Allocate a block descriptor */
static block_info_t *alloc_block(void) {
    if (pmm_state.block_count >= MAX_BLOCK_DESC) {
        return 0;
    }
    return &pmm_state.blocks[pmm_state.block_count++];
}

/* Add a free block to the appropriate free list */
static void add_free_block(uint64_t addr, uint8_t order) {
    block_info_t *block = alloc_block();

    if (!block) {
        serial_puts("PMM: ERROR - Out of block descriptors\n");
        return;
    }

    block->base_addr = addr;
    block->order = order;
    block->allocated = 0;
    list_init(&block->list);

    list_push_back(&pmm_state.free_lists[order].list, &block->list);
    pmm_state.free_lists[order].count++;
    pmm_state.free_pages += (1 << order);
}

/* Remove a block from free list */
static block_info_t *remove_free_block(uint8_t order) {
    struct list_head *node = list_pop_front(&pmm_state.free_lists[order].list);

    if (!node) {
        return 0;
    }

    pmm_state.free_lists[order].count--;
    block_info_t *block = list_entry(node, block_info_t, list);
    pmm_state.free_pages -= (1 << order);
    return block;
}

/* Split a block into two smaller blocks */
static block_info_t *split_block(block_info_t *block, uint8_t target_order) {
    while (block->order > target_order) {
        /* Remove from current free list */
        list_remove(&block->list);
        pmm_state.free_lists[block->order].count--;

        /* Decrease order */
        block->order--;

        /* Create buddy block */
        uint64_t buddy_addr = block->base_addr + (PAGE_SIZE << block->order);
        add_free_block(buddy_addr, block->order);

        /* Add back to new free list */
        list_push_back(&pmm_state.free_lists[block->order].list, &block->list);
        pmm_state.free_lists[block->order].count++;
    }

    /* Remove from free list and mark as allocated */
    list_remove(&block->list);
    pmm_state.free_lists[block->order].count--;
    block->allocated = 1;
    list_push_back(&pmm_state.allocated_blocks, &block->list);

    return block;
}

/* Find best fit free block for given order */
static block_info_t *find_free_block(uint8_t order) {
    struct list_head *pos;
    block_info_t *block;

    /* Search for exact fit or larger block */
    for (uint8_t o = order; o <= MAX_ORDER; o++) {
        if (!list_empty(&pmm_state.free_lists[o].list)) {
            pos = pmm_state.free_lists[o].list.next;
            block = list_entry(pos, block_info_t, list);
            return split_block(block, order);
        }
    }

    return 0;
}

/* Coalesce a block with its buddy */
static void coalesce_block(block_info_t *block) {
    uint64_t buddy_addr;
    block_info_t *buddy;
    struct list_head *pos;

    while (block->order < MAX_ORDER) {
        buddy_addr = get_buddy_addr(block->base_addr, block->order);

        /* Search for buddy in free list */
        buddy = 0;
        list_for_each(pos, &pmm_state.free_lists[block->order].list) {
            block_info_t *b = list_entry(pos, block_info_t, list);
            if (b->base_addr == buddy_addr && !b->allocated) {
                buddy = b;
                break;
            }
        }

        /* Buddy not free or not found - can't coalesce */
        if (!buddy) {
            break;
        }

        /* Remove buddy from free list */
        list_remove(&buddy->list);
        pmm_state.free_lists[block->order].count--;

        /* Make sure we have the lower address */
        if (block->base_addr > buddy_addr) {
            block->base_addr = buddy_addr;
        }

        /* Increase order */
        block->order++;
    }

    /* Add coalesced block to free list */
    block->allocated = 0;
    list_push_back(&pmm_state.free_lists[block->order].list, &block->list);
    pmm_state.free_lists[block->order].count++;
    pmm_state.free_pages += (1 << block->order);
}

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * pmm_init - Initialize Physical Memory Manager
 * @mbi_addr: Multiboot2 info structure address
 */
void pmm_init(uint32_t mbi_addr) {
    serial_puts("PMM: Initializing...\n");

    /* Initialize spin lock */
    spin_lock_init(&pmm_state.lock);

    /* Initialize state */
    for (int i = 0; i <= MAX_ORDER; i++) {
        list_init(&pmm_state.free_lists[i].list);
        pmm_state.free_lists[i].count = 0;
    }
    list_init(&pmm_state.allocated_blocks);
    list_init(&pmm_state.regions);
    pmm_state.total_pages = 0;
    pmm_state.free_pages = 0;
    pmm_state.block_count = 0;

    /* Parse multiboot2 memory map */
    multiboot2_parse(mbi_addr);

    /* Reserve kernel region */
    uint64_t kernel_start = (uint64_t)_kernel_start;
    uint64_t kernel_end = (uint64_t)_kernel_end;
    uint64_t kernel_size = kernel_end - kernel_start;

    serial_puts("PMM: Reserving kernel at 0x");
    put_hex(kernel_start);
    serial_puts(", size ");
    put_hex(kernel_size);
    serial_puts(" bytes\n");
    pmm_reserve_region(kernel_start, kernel_size);

    /* Reserve AP trampoline (0x7000 - 0x9000) */
    serial_puts("PMM: Reserving trampoline at 0x7000, size 8192 bytes\n");
    pmm_reserve_region(0x7000, 8192);

    /* Reserve boot stack area */
    serial_puts("PMM: Reserving boot stacks, size 32768 bytes\n");
    pmm_reserve_region(kernel_end, 32768);

    serial_puts("PMM: Initialized with ");
    put_hex(pmm_state.total_pages * PAGE_SIZE);
    serial_puts(" bytes total, ");
    put_hex(pmm_state.free_pages * PAGE_SIZE);
    serial_puts(" bytes free\n");
}

/**
 * pmm_add_region - Add a memory region to management
 * @base: Physical base address of region
 * @size: Size of region in bytes
 */
void pmm_add_region(uint64_t base, uint64_t size) {
    mem_region_t *region;
    uint64_t addr;
    uint64_t end;
    uint8_t order;

    serial_puts("PMM: Adding region 0x");
    put_hex(base);
    serial_puts(" - 0x");
    put_hex(base + size);
    serial_puts("\n");

    /* Align to page boundaries */
    base = (base + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    size = size & ~(PAGE_SIZE - 1);

    if (size < PAGE_SIZE) {
        return;
    }

    /* Track region */
    /* Note: For simplicity, we don't track regions in a list yet
     * This could be added later for better memory management */

    end = base + size;
    addr = base;

    /* Add pages to free lists using largest possible blocks */
    while (addr < end) {
        uint64_t remaining = end - addr;

        /* Find largest order that fits */
        for (order = MAX_ORDER; order > 0; order--) {
            uint64_t block_size = PAGE_SIZE << order;
            if (remaining >= block_size && (addr & (block_size - 1)) == 0) {
                add_free_block(addr, order);
                addr += block_size;
                break;
            }
        }

        /* Handle remaining small pages */
        if (order == 0 && remaining >= PAGE_SIZE) {
            add_free_block(addr, 0);
            addr += PAGE_SIZE;
        } else if (order == 0) {
            break;
        }
    }

    pmm_state.total_pages += size / PAGE_SIZE;
}

/**
 * pmm_reserve_region - Reserve a memory region (mark as used)
 * @base: Physical base address of region
 * @size: Size of region in bytes
 */
void pmm_reserve_region(uint64_t base, uint64_t size) {
    uint64_t addr = base;
    uint64_t end = base + size;
    struct list_head *pos, *next;

    /* Align to page boundaries */
    addr = (addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    end = end & ~(PAGE_SIZE - 1);

    if (addr >= end) {
        return;
    }

    /* Find and remove free pages that intersect with reserved region */
    for (uint8_t order = 0; order <= MAX_ORDER; order++) {
        list_for_each_safe(pos, next, &pmm_state.free_lists[order].list) {
            block_info_t *block = list_entry(pos, block_info_t, list);
            uint64_t block_start = block->base_addr;
            uint64_t block_end = block_start + (PAGE_SIZE << order);

            /* Check intersection */
            if (addr < block_end && end > block_start) {
                /* Remove block from free list */
                list_remove(pos);
                pmm_state.free_lists[order].count--;
                pmm_state.free_pages -= (1 << order);

                /* Add partial regions back if needed */
                if (block_start < addr) {
                    uint64_t prefix_size = addr - block_start;
                    /* Re-add prefix as smaller blocks */
                    uint64_t prefix_addr = block_start;
                    while (prefix_size >= PAGE_SIZE) {
                        for (uint8_t o = 0; o <= order; o++) {
                            uint64_t bs = PAGE_SIZE << o;
                            if (prefix_size >= bs && (prefix_addr & (bs - 1)) == 0) {
                                add_free_block(prefix_addr, o);
                                prefix_addr += bs;
                                prefix_size -= bs;
                                break;
                            }
                        }
                    }
                }

                if (block_end > end) {
                    uint64_t suffix_size = block_end - end;
                    /* Re-add suffix as smaller blocks */
                    uint64_t suffix_addr = end;
                    while (suffix_size >= PAGE_SIZE) {
                        for (uint8_t o = 0; o <= order; o++) {
                            uint64_t bs = PAGE_SIZE << o;
                            if (suffix_size >= bs && (suffix_addr & (bs - 1)) == 0) {
                                add_free_block(suffix_addr, o);
                                suffix_addr += bs;
                                suffix_size -= bs;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
}

/**
 * pmm_alloc - Allocate physical pages
 * @order: Order of allocation (log2 of number of pages)
 *
 * Returns: Physical address of allocated block, or NULL if out of memory
 */
void *pmm_alloc(uint8_t order) {
    block_info_t *block;
    irq_flags_t flags;

    if (order > MAX_ORDER) {
        return 0;
    }

    /* Protect PMM operation with interrupt-safe lock */
    spin_lock_irqsave(&pmm_state.lock, &flags);

    block = find_free_block(order);
    if (!block) {
        spin_unlock_irqrestore(&pmm_state.lock, &flags);
        serial_puts("PMM: Out of memory for order ");
        put_hex(order);
        serial_puts("\n");
        return 0;
    }

    spin_unlock_irqrestore(&pmm_state.lock, &flags);

    return (void *)block->base_addr;
}

/**
 * pmm_free - Free physical pages
 * @phys_addr: Physical address of block to free
 * @order: Order of allocation (must match original allocation)
 */
void pmm_free(void *phys_addr, uint8_t order) {
    block_info_t *block;
    uint64_t addr = (uint64_t)phys_addr;
    irq_flags_t flags;

    if (order > MAX_ORDER) {
        return;
    }

    /* Protect PMM operation with interrupt-safe lock */
    spin_lock_irqsave(&pmm_state.lock, &flags);

    /* Find block in allocated list */
    block = find_allocated_block(addr);
    if (!block) {
        spin_unlock_irqrestore(&pmm_state.lock, &flags);
        serial_puts("PMM: WARNING - Freeing unallocated block at 0x");
        put_hex(addr);
        serial_puts("\n");
        return;
    }

    /* Remove from allocated list */
    list_remove(&block->list);

    /* Coalesce with buddies and add to free list */
    coalesce_block(block);

    spin_unlock_irqrestore(&pmm_state.lock, &flags);
}

/**
 * pmm_get_free_pages - Get number of free pages
 *
 * Returns: Number of currently free pages
 */
uint64_t pmm_get_free_pages(void) {
    return pmm_state.free_pages;
}

/**
 * pmm_get_total_pages - Get total number of pages
 *
 * Returns: Total number of pages in all managed regions
 */
uint64_t pmm_get_total_pages(void) {
    return pmm_state.total_pages;
}
