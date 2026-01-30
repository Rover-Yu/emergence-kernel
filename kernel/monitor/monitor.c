/* monitor.c - Monitor core implementation for nested kernel architecture */

#include "monitor.h"

/* External functions */
extern void serial_puts(const char *str);
extern void *pmm_alloc(uint8_t order);
extern void pmm_free(void *addr, uint8_t order);

/* Page table physical addresses */
uint64_t monitor_pml4_phys = 0;
uint64_t unpriv_pml4_phys = 0;

/* Monitor page table structures (allocated during init) */
static uint64_t *monitor_pml4;
static uint64_t *monitor_pdpt;
static uint64_t *monitor_pd;

static uint64_t *unpriv_pml4;
static uint64_t *unpriv_pdpt;
static uint64_t *unpriv_pd;

/* External boot page table symbols (from boot.S) - these are 4KB arrays */
extern uint64_t boot_pml4[];
extern uint64_t boot_pdpt[];
extern uint64_t boot_pd[];

/* Get physical address from virtual (identity mapped) */
static inline uint64_t virt_to_phys(void *virt) {
    return (uint64_t)virt;
}

/* Initialize monitor page tables */
void monitor_init(void) {
    serial_puts("MONITOR: Initializing nested kernel architecture\n");

    /* Allocate page tables for monitor (privileged) view */
    monitor_pml4 = (uint64_t *)pmm_alloc(0);  /* 1 page */
    monitor_pdpt = (uint64_t *)pmm_alloc(0);
    monitor_pd = (uint64_t *)pmm_alloc(0);

    if (!monitor_pml4 || !monitor_pdpt || !monitor_pd) {
        serial_puts("MONITOR: Failed to allocate monitor page tables\n");
        return;
    }

    /* Allocate page tables for unprivileged view */
    unpriv_pml4 = (uint64_t *)pmm_alloc(0);
    unpriv_pdpt = (uint64_t *)pmm_alloc(0);
    unpriv_pd = (uint64_t *)pmm_alloc(0);

    if (!unpriv_pml4 || !unpriv_pdpt || !unpriv_pd) {
        serial_puts("MONITOR: Failed to allocate unprivileged page tables\n");
        return;
    }

    /* Copy boot page table mappings to monitor view */
    for (int i = 0; i < 512; i++) {
        monitor_pml4[i] = boot_pml4[i];
        monitor_pdpt[i] = boot_pdpt[i];
        monitor_pd[i] = boot_pd[i];

        unpriv_pml4[i] = boot_pml4[i];
        unpriv_pdpt[i] = boot_pdpt[i];
        unpriv_pd[i] = boot_pd[i];
    }

    /* Save physical addresses */
    monitor_pml4_phys = virt_to_phys(monitor_pml4);
    unpriv_pml4_phys = virt_to_phys(unpriv_pml4);

    serial_puts("MONITOR: Page tables initialized\n");
    serial_puts("MONITOR: APIC accessible from unprivileged mode\n");
}

/* Get unprivileged CR3 value */
uint64_t monitor_get_unpriv_cr3(void) {
    return unpriv_pml4_phys;
}

/* Check if running in privileged (monitor) mode */
bool monitor_is_privileged(void) {
    uint64_t cr3;
    asm volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3 == monitor_pml4_phys;
}

/* Internal monitor call handler (called from privileged context only) */
monitor_ret_t monitor_call_handler(monitor_call_t call, uint64_t arg1,
                                     uint64_t arg2, uint64_t arg3) {
    monitor_ret_t ret = {0, 0};

    switch (call) {
        case MONITOR_CALL_ALLOC_PHYS:
            /* Direct PMM access (already in privileged mode) */
            ret.result = (uint64_t)pmm_alloc((uint8_t)arg1);
            if (!ret.result) {
                ret.error = -1;
            }
            break;

        case MONITOR_CALL_FREE_PHYS:
            /* Direct PMM access (already in privileged mode) */
            pmm_free((void *)arg1, (uint8_t)arg2);
            break;

        default:
            ret.error = -1;
            break;
    }

    return ret;
}

/* External assembly stub for monitor calls (CR3 switching) */
extern monitor_ret_t monitor_call_stub(monitor_call_t call, uint64_t arg1,
                                        uint64_t arg2, uint64_t arg3);

/* Public monitor call wrapper (for unprivileged code) */
monitor_ret_t monitor_call(monitor_call_t call, uint64_t arg1,
                            uint64_t arg2, uint64_t arg3) {
    if (monitor_pml4_phys == 0) {
        /* Monitor not initialized yet, call directly */
        return monitor_call_handler(call, arg1, arg2, arg3);
    }

    if (monitor_is_privileged()) {
        /* Already privileged, call directly */
        return monitor_call_handler(call, arg1, arg2, arg3);
    }

    /* Unprivileged: use assembly stub to switch CR3 */
    return monitor_call_stub(call, arg1, arg2, arg3);
}

/* PMM monitor call wrappers */
void *monitor_pmm_alloc(uint8_t order) {
    monitor_ret_t ret = monitor_call(MONITOR_CALL_ALLOC_PHYS, order, 0, 0);
    return (void *)ret.result;
}

void monitor_pmm_free(void *addr, uint8_t order) {
    monitor_call(MONITOR_CALL_FREE_PHYS, (uint64_t)addr, order, 0);
}
