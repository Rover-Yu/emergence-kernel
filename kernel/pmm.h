/* Emergence Kernel - Physical Memory Manager (Buddy System) */

#ifndef _KERNEL_PMM_H
#define _KERNEL_PMM_H

#include <stdint.h>
#include "kernel/list.h"
#include "include/spinlock.h"

/* Page size and shift */
#define PAGE_SIZE       4096
#define PAGE_SHIFT      12

/* Maximum order for buddy system (2^9 = 512 pages = 2MB) */
#define MAX_ORDER       9

/* Maximum number of block descriptors for metadata */
#define MAX_BLOCK_DESC  1024

/* Forward declarations */
typedef struct pmm_state pmm_state_t;

/* Block descriptor - tracks a memory block */
typedef struct block_info {
    uint64_t base_addr;           /* Physical base address */
    uint8_t order;                /* Order (log2 of pages in block) */
    uint8_t allocated;            /* 1 if allocated, 0 if free */
    uint8_t reserved;             /* Padding */
    struct list_head list;        /* Free list or allocated list */
} block_info_t;

/* Memory region for tracking managed areas */
typedef struct mem_region {
    uint64_t base;
    uint64_t size;
    struct list_head list;
} mem_region_t;

/* Free list for each order */
typedef struct free_list {
    struct list_head list;
    uint64_t count;
} free_list_t;

/* PMM state structure */
typedef struct pmm_state {
    spinlock_t lock;                        /* Protects PMM operations */
    free_list_t free_lists[MAX_ORDER + 1];   /* Free lists for each order */
    struct list_head allocated_blocks;       /* List of allocated blocks */
    struct list_head regions;                /* List of managed regions */
    uint64_t total_pages;                    /* Total pages in all regions */
    uint64_t free_pages;                     /* Currently free pages */
    block_info_t blocks[MAX_BLOCK_DESC];     /* Static block descriptor array */
    uint64_t block_count;                    /* Number of blocks in use */
} pmm_state_t;

/* Function prototypes */

/* Initialization */
void pmm_init(uint32_t mbi_addr);
void pmm_add_region(uint64_t base, uint64_t size);
void pmm_reserve_region(uint64_t base, uint64_t size);

/* Allocation/Free */
void *pmm_alloc(uint8_t order);
void pmm_free(void *phys_addr, uint8_t order);

/* Statistics */
uint64_t pmm_get_free_pages(void);
uint64_t pmm_get_total_pages(void);

#endif /* _KERNEL_PMM_H */
