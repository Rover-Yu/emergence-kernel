/* Emergence Kernel - Slab Allocator */

#ifndef _KERNEL_SLAB_H
#define _KERNEL_SLAB_H

#include <stdint.h>
#include <stddef.h>
#include "kernel/list.h"
#include "include/spinlock.h"

/* Page size from PMM */
#define PAGE_SIZE       4096
#define PAGE_SHIFT      12

/* Maximum number of slab caches */
#define SLAB_NR_CACHES  8

/* Slab cache sizes (power of two: 32B to 4KB) */
#define SLAB_SIZE_32     32
#define SLAB_SIZE_64     64
#define SLAB_SIZE_128    128
#define SLAB_SIZE_256    256
#define SLAB_SIZE_512    512
#define SLAB_SIZE_1024   1024
#define SLAB_SIZE_2048   2048
#define SLAB_SIZE_4096   4096

/* ============================================================================
 * Data Structures
 * ============================================================================ */

/*
 * Free object header - embedded in free objects
 * When an object is free, we use this space to track the free list
 */
typedef struct slab_obj {
    struct list_head list;       /* Free list linkage */
} slab_obj_t;

/* Forward declaration */
typedef struct slab_cache slab_cache_t;

/*
 * slab - represents a slab (one page) containing objects
 *
 * The slab_t structure is stored at the beginning of each page.
 * This allows us to recover the slab structure from any object pointer
 * by masking the page-aligned address.
 */
typedef struct slab {
    struct list_head list;       /* List node (full/partial/free) */
    slab_cache_t *cache;         /* Parent cache */
    void *base_addr;             /* Base virtual address of slab */
    uint64_t inuse;              /* Number of objects in use */
    struct list_head free_list;  /* List of free objects in this slab */
} slab_t;

/*
 * slab_cache - represents a cache for objects of a specific size
 *
 * Each cache manages slabs of a fixed object size.
 * Slabs are organized into three lists:
 * - slabs_full: All objects allocated
 * - slabs_partial: Some objects allocated, some free
 * - slabs_free: No objects allocated (empty slab)
 */
typedef struct slab_cache {
    size_t object_size;              /* Size of each object */
    size_t objects_per_slab;         /* Number of objects per slab */
    struct list_head slabs_full;     /* List of full slabs */
    struct list_head slabs_partial;  /* List of partially full slabs */
    struct list_head slabs_free;     /* List of empty slabs */
    spinlock_t lock;                 /* Protects cache operations */
    uint64_t total_objects;          /* Total objects across all slabs */
    uint64_t free_objects;           /* Currently free objects */
    struct list_head list;           /* For cache registry */
} slab_cache_t;

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * slab_init - Initialize the slab allocator
 *
 * Must be called after pmm_init().
 * Creates 8 power-of-two caches for sizes 32, 64, 128, 256, 512,
 * 1024, 2048, and 4096 bytes.
 */
void slab_init(void);

/**
 * slab_cache_create - Initialize a slab cache
 * @cache: Cache structure to initialize
 * @object_size: Size of each object (must be power of 2)
 *
 * Returns: 0 on success, -1 on error
 */
int slab_cache_create(slab_cache_t *cache, size_t object_size);

/**
 * slab_alloc - Allocate an object from a specific cache
 * @cache: Cache to allocate from
 *
 * Returns: Pointer to allocated object, or NULL if out of memory
 */
void *slab_alloc(slab_cache_t *cache);

/**
 * slab_free - Free an object back to its cache
 * @cache: Cache the object belongs to
 * @obj: Object to free
 */
void slab_free(slab_cache_t *cache, void *obj);

/**
 * slab_alloc_size - Allocate object with size rounding
 * @size: Desired size
 *
 * Allocates from the smallest cache that can satisfy the request.
 * Size is rounded up to the next power of two.
 *
 * Returns: Pointer to allocated object, or NULL if out of memory
 */
void *slab_alloc_size(size_t size);

/**
 * slab_free_size - Free object allocated with slab_alloc_size
 * @ptr: Object to free
 * @size: Original size requested (for cache selection)
 */
void slab_free_size(void *ptr, size_t size);

/**
 * slab_dump_stats - Dump slab allocator statistics
 *
 * Prints statistics for each cache to serial console.
 */
void slab_dump_stats(void);

#endif /* _KERNEL_SLAB_H */
