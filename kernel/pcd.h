/* Emergence Kernel - Page Control Data (PCD) for Nested Kernel */

#ifndef _KERNEL_PCD_H
#define _KERNEL_PCD_H

#include <stdint.h>
#include <stdbool.h>
#include "include/spinlock.h"

/* ============================================================================
 * PCD Page Types
 * ============================================================================ */

#define PCD_TYPE_OK_NORMAL   0  /* Outer kernel normal pages (code, data, heap) */
#define PCD_TYPE_NK_NORMAL   1  /* Monitor private pages */
#define PCD_TYPE_NK_PGTABLE  2  /* Page table pages (monitor-controlled) */
#define PCD_TYPE_NK_IO       3  /* I/O register mappings (tracked but not enforced) */

#define PCD_TYPE_MIN         0
#define PCD_TYPE_MAX         3

/* ============================================================================
 * PCD Structure - Per-Page Metadata
 * ============================================================================ */

/* PCD structure - packed to 8 bytes per page */
typedef struct __attribute__((packed)) {
    uint8_t  type;           /* Page type (PCD_TYPE_*) */
    uint8_t  flags;          /* Additional flags (reserved for future use) */
    uint16_t reserved;       /* Future expansion */
    uint32_t refcount;       /* Reference count for shared pages */
} pcd_t;

/* PCD flags */
#define PCD_FLAG_RESERVED    0x01  /* Page is reserved (do not allocate) */
#define PCD_FLAG_LOCKED      0x02  /* Page type cannot be changed */

/* PCD state structure */
typedef struct {
    pcd_t    *pages;         /* Array of PCDs (dynamically allocated) */
    uint64_t  max_pages;     /* Number of physical pages managed */
    uint64_t  base_page;     /* First physical page managed by PCD */
    spinlock_t lock;         /* Protect PCD modifications */
    bool     initialized;    /* PCD system is ready */
} pcd_state_t;

/* ============================================================================
 * PCD Public API
 * ============================================================================ */

/* Initialization */
void pcd_init(void);

/* Type management - read-only access for outer kernel */
/* Note: pcd_set_type() is monitor-only and not exposed to outer kernel */
uint8_t pcd_get_type(uint64_t phys_addr);

/* Region marking */
void pcd_mark_region(uint64_t base, uint64_t size, uint8_t type);

/* Query functions */
bool pcd_is_initialized(void);
uint64_t pcd_get_max_pages(void);

/* Debug/diagnostic */
void pcd_dump_stats(void);

#endif /* _KERNEL_PCD_H */
