/* Emergence Kernel - Slab Allocator Implementation */

#include <stdint.h>
#include "kernel/slab.h"
#include "kernel/pmm.h"
#include "arch/x86_64/serial.h"

/* ============================================================================
 * Global State
 * ============================================================================ */

/* Array of power-of-two caches */
static slab_cache_t slab_caches[SLAB_NR_CACHES];

/* Cache sizes */
static const size_t cache_sizes[SLAB_NR_CACHES] = {
    SLAB_SIZE_32,
    SLAB_SIZE_64,
    SLAB_SIZE_128,
    SLAB_SIZE_256,
    SLAB_SIZE_512,
    SLAB_SIZE_1024,
    SLAB_SIZE_2048,
    SLAB_SIZE_4096
};

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

/**
 * size_to_cache_index - Convert size to cache index
 * @size: Desired allocation size
 *
 * Returns the index of the smallest cache that can satisfy the request.
 * Sizes are rounded up to the next power of two.
 */
static int size_to_cache_index(size_t size) {
    if (size <= SLAB_SIZE_32) return 0;
    if (size <= SLAB_SIZE_64) return 1;
    if (size <= SLAB_SIZE_128) return 2;
    if (size <= SLAB_SIZE_256) return 3;
    if (size <= SLAB_SIZE_512) return 4;
    if (size <= SLAB_SIZE_1024) return 5;
    if (size <= SLAB_SIZE_2048) return 6;
    if (size <= SLAB_SIZE_4096) return 7;
    return -1;  /* Size too large */
}

/**
 * slab_new - Allocate a new slab from PMM
 * @cache: Cache to create slab for
 *
 * Allocates a page from PMM and initializes it as a slab.
 * The slab_t structure is placed at the beginning of the page.
 *
 * Returns: Pointer to new slab, or NULL if out of memory
 */
static slab_t *slab_new(slab_cache_t *cache) {
    slab_t *slab;
    void *page_addr;
    char *obj_ptr;
    size_t i;

    /* Allocate a page from PMM (order 0 = 1 page) */
    page_addr = pmm_alloc(0);
    if (page_addr == NULL) {
        serial_puts("SLAB: Failed to allocate page from PMM\n");
        return NULL;
    }

    /* Initialize slab structure at start of page */
    slab = (slab_t *)page_addr;
    slab->cache = cache;
    slab->base_addr = page_addr;
    slab->inuse = 0;
    list_init(&slab->free_list);

    /* Calculate object area start (after slab_t metadata) */
    obj_ptr = (char *)page_addr + sizeof(slab_t);

    /* Calculate usable space in page */
    size_t usable_space = PAGE_SIZE - sizeof(slab_t);
    size_t objects_per_slab = usable_space / cache->object_size;

    /* Initialize all objects as free */
    for (i = 0; i < objects_per_slab; i++) {
        slab_obj_t *obj = (slab_obj_t *)obj_ptr;
        list_push_back(&slab->free_list, &obj->list);
        obj_ptr += cache->object_size;
    }

    /* Update cache statistics */
    cache->total_objects += objects_per_slab;
    cache->free_objects += objects_per_slab;

    serial_puts("SLAB: Created new slab at 0x");
    serial_put_hex((uint64_t)slab);
    serial_puts(" with ");
    serial_put_hex(objects_per_slab);
    serial_puts(" objects of size ");
    serial_put_hex(cache->object_size);
    serial_puts("\n");

    return slab;
}

/* ============================================================================
 * Public API Implementation
 * ============================================================================ */

/**
 * slab_init - Initialize the slab allocator
 */
void slab_init(void) {
    int i;

    serial_puts("SLAB: Initializing slab allocator...\n");

    /* Initialize each cache */
    for (i = 0; i < SLAB_NR_CACHES; i++) {
        slab_cache_create(&slab_caches[i], cache_sizes[i]);
    }

    serial_puts("SLAB: Initialized ");
    serial_put_hex(SLAB_NR_CACHES);
    serial_puts(" caches\n");
}

/**
 * slab_cache_create - Initialize a slab cache
 */
int slab_cache_create(slab_cache_t *cache, size_t object_size) {
    if (cache == NULL || object_size == 0 || object_size > PAGE_SIZE) {
        return -1;
    }

    /* Verify power of two */
    if (object_size & (object_size - 1)) {
        serial_puts("SLAB: Object size not power of two: ");
        serial_put_hex(object_size);
        serial_puts("\n");
        return -1;
    }

    /* Initialize cache structure */
    cache->object_size = object_size;

    /* Calculate objects per slab */
    size_t usable_space = PAGE_SIZE - sizeof(slab_t);
    cache->objects_per_slab = usable_space / object_size;

    /* Initialize lists */
    list_init(&cache->slabs_full);
    list_init(&cache->slabs_partial);
    list_init(&cache->slabs_free);

    /* Initialize spinlock */
    spin_lock_init(&cache->lock);

    /* Initialize statistics */
    cache->total_objects = 0;
    cache->free_objects = 0;

    serial_puts("SLAB: Created cache for size ");
    serial_put_hex(object_size);
    serial_puts(" (");
    serial_put_hex(cache->objects_per_slab);
    serial_puts(" objects/slab)\n");

    return 0;
}

/**
 * slab_alloc - Allocate an object from a specific cache
 */
void *slab_alloc(slab_cache_t *cache) {
    slab_t *slab;
    slab_obj_t *obj;
    irq_flags_t flags;

    if (cache == NULL) {
        return NULL;
    }

    /* Lock the cache */
    spin_lock_irqsave(&cache->lock, &flags);

    /* Try partial slabs first */
    if (!list_empty(&cache->slabs_partial)) {
        slab = list_entry(cache->slabs_partial.next, slab_t, list);
        obj = list_entry(slab->free_list.next, slab_obj_t, list);

        /* Remove from free list */
        list_remove(&obj->list);
        slab->inuse++;
        cache->free_objects--;

        /* Check if slab became full */
        if (list_empty(&slab->free_list)) {
            list_remove(&slab->list);
            list_push_front(&cache->slabs_full, &slab->list);
        }

        spin_unlock_irqrestore(&cache->lock, &flags);
        return (void *)obj;
    }

    /* Try free slabs */
    if (!list_empty(&cache->slabs_free)) {
        slab = list_entry(cache->slabs_free.next, slab_t, list);
        obj = list_entry(slab->free_list.next, slab_obj_t, list);

        /* Remove from free list */
        list_remove(&obj->list);
        slab->inuse++;
        cache->free_objects--;

        /* Move to partial list */
        list_remove(&slab->list);
        list_push_front(&cache->slabs_partial, &slab->list);

        spin_unlock_irqrestore(&cache->lock, &flags);
        return (void *)obj;
    }

    /* Need to allocate new slab */
    spin_unlock_irqrestore(&cache->lock, &flags);

    slab = slab_new(cache);
    if (slab == NULL) {
        return NULL;
    }

    /* Re-lock and add slab to partial list */
    spin_lock_irqsave(&cache->lock, &flags);

    /* Get first free object */
    obj = list_entry(slab->free_list.next, slab_obj_t, list);
    list_remove(&obj->list);
    slab->inuse++;
    cache->free_objects--;

    /* Add to partial list */
    list_push_front(&cache->slabs_partial, &slab->list);

    spin_unlock_irqrestore(&cache->lock, &flags);

    return (void *)obj;
}

/**
 * slab_free - Free an object back to its cache
 */
void slab_free(slab_cache_t *cache, void *obj_ptr) {
    slab_t *slab;
    slab_obj_t *obj;
    irq_flags_t flags;
    void *page_base;

    if (cache == NULL || obj_ptr == NULL) {
        return;
    }

    /* Get page base (slab_t is at start of page) */
    page_base = (void *)((uint64_t)obj_ptr & ~(PAGE_SIZE - 1));
    slab = (slab_t *)page_base;

    /* Verify slab belongs to cache */
    if (slab->cache != cache) {
        serial_puts("SLAB: Warning - object freed to wrong cache\n");
        return;
    }

    obj = (slab_obj_t *)obj_ptr;

    spin_lock_irqsave(&cache->lock, &flags);

    /* Determine which list slab is on */
    int was_full = !list_empty(&slab->list) &&
                   (slab->list.next == &cache->slabs_full ||
                    slab->list.prev == &cache->slabs_full);

    /* Add object to slab's free list */
    list_push_front(&slab->free_list, &obj->list);
    slab->inuse--;
    cache->free_objects++;

    /* Move slab to appropriate list */
    if (was_full) {
        /* Was full, now partial */
        list_remove(&slab->list);
        list_push_front(&cache->slabs_partial, &slab->list);
    } else if (slab->inuse == 0) {
        /* Now empty, move to free list */
        list_remove(&slab->list);
        list_push_front(&cache->slabs_free, &slab->list);
    }

    spin_unlock_irqrestore(&cache->lock, &flags);
}

/**
 * slab_alloc_size - Allocate object with size rounding
 */
void *slab_alloc_size(size_t size) {
    int idx;

    if (size == 0) {
        return NULL;
    }

    idx = size_to_cache_index(size);
    if (idx < 0) {
        serial_puts("SLAB: Size too large: ");
        serial_put_hex(size);
        serial_puts("\n");
        return NULL;
    }

    return slab_alloc(&slab_caches[idx]);
}

/**
 * slab_free_size - Free object allocated with slab_alloc_size
 */
void slab_free_size(void *ptr, size_t size) {
    int idx;

    if (ptr == NULL || size == 0) {
        return;
    }

    idx = size_to_cache_index(size);
    if (idx < 0) {
        return;
    }

    slab_free(&slab_caches[idx], ptr);
}

/**
 * slab_dump_stats - Dump slab allocator statistics
 */
void slab_dump_stats(void) {
    int i;

    serial_puts("\n=== SLAB Allocator Statistics ===\n");

    for (i = 0; i < SLAB_NR_CACHES; i++) {
        slab_cache_t *cache = &slab_caches[i];

        serial_puts("Cache[");
        serial_putc('0' + i);
        serial_puts("] size=");
        serial_put_hex(cache->object_size);
        serial_puts(" total=");
        serial_put_hex(cache->total_objects);
        serial_puts(" free=");
        serial_put_hex(cache->free_objects);
        serial_puts("\n");
    }

    serial_puts("==================================\n\n");
}
