#ifndef KERNEL_MONITOR_MONITOR_H
#define KERNEL_MONITOR_MONITOR_H

#include <stdint.h>
#include <stdbool.h>

/* Monitor call return structure */
typedef struct {
    uint64_t result;
    int error;
} monitor_ret_t;

/* Monitor call types */
typedef enum {
    MONITOR_CALL_ALLOC_PHYS,    /* Allocate physical memory */
    MONITOR_CALL_FREE_PHYS,     /* Free physical memory */
    /* Future: page table manipulation calls */
} monitor_call_t;

/* Monitor page table physical addresses (set by monitor_init) */
extern uint64_t monitor_pml4_phys;    /* Full privileged view */
extern uint64_t unpriv_pml4_phys;     /* Restricted unprivileged view */

/* Monitor initialization */
void monitor_init(void);

/* Verify Nested Kernel invariants (called after CR0.WP is set) */
void monitor_verify_invariants(void);

/* Get unprivileged CR3 value for switching */
uint64_t monitor_get_unpriv_cr3(void);

/* Check if running in monitor mode (privileged) */
bool monitor_is_privileged(void);

/* Monitor call handler (called from unprivileged mode) */
monitor_ret_t monitor_call(monitor_call_t call, uint64_t arg1, uint64_t arg2, uint64_t arg3);

/* PMM monitor calls */
void *monitor_pmm_alloc(uint8_t order);
void monitor_pmm_free(void *addr, uint8_t order);

#endif /* KERNEL_MONITOR_MONITOR_H */
