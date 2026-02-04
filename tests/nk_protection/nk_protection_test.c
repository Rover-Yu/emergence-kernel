/* nk_protection_test.c - Nested Kernel Mappings Protection Tests */

#include <stdint.h>
#include "arch/x86_64/serial.h"
#include "arch/x86_64/paging.h"

/* External symbols - protected nested kernel regions */
extern uint64_t boot_pml4[];
extern uint64_t boot_pdpt[];
extern uint64_t boot_pd[];
extern uint64_t monitor_pml4_phys;
extern uint64_t unpriv_pml4_phys;
extern void kernel_main(void);

/* External nested kernel symbols from linker */
extern char _kernel_start[];     /* Start of kernel code/data */
extern char _kernel_end[];       /* End of kernel code/data */
extern char ok_ap_boot_trampoline[];     /* AP boot trampoline (outer kernel code) */
extern char ok_ap_boot_trampoline_end[];
extern uint8_t nk_boot_stack_bottom[];  /* Nested kernel boot stack */
extern uint8_t nk_boot_stack_top[];

/* External monitor call stub (nested kernel entry trampoline) */
extern void nk_entry_trampoline(void);

/* Test: Write to protected page table */
static void test_write_pagetable(const char *name, volatile uint64_t *addr) {
    serial_puts("[NK-PROTECTION TEST] Writing to ");
    serial_puts(name);
    serial_puts(" at 0x");
    serial_put_hex((uint64_t)addr);
    serial_puts("\n");
    *addr = 0xDEADBEEF;  /* Should fault */
    serial_puts("[NK-PROTECTION TEST] ERROR: Write succeeded!\n");
}

/* Test: Write to code segment */
static void test_write_code_segment(const char *name, void *addr) {
    serial_puts("[NK-PROTECTION TEST] Writing to code segment ");
    serial_puts(name);
    serial_puts(" at 0x");
    serial_put_hex((uint64_t)addr);
    serial_puts("\n");
    volatile uint64_t *code_addr = (uint64_t *)addr;
    *code_addr = 0xDEADBEEF;  /* Should fault */
    serial_puts("[NK-PROTECTION TEST] ERROR: Code write succeeded!\n");
}

/* Test: Write to data segment */
static void test_write_data_segment(const char *name, void *addr) {
    serial_puts("[NK-PROTECTION TEST] Writing to data segment ");
    serial_puts(name);
    serial_puts(" at 0x");
    serial_put_hex((uint64_t)addr);
    serial_puts("\n");
    volatile uint64_t *data_addr = (uint64_t *)addr;
    *data_addr = 0xDEADBEEF;  /* Should fault */
    serial_puts("[NK-PROTECTION TEST] ERROR: Data write succeeded!\n");
}

/* Test: Write to stack */
static void test_write_stack(const char *name, volatile uint64_t *addr) {
    serial_puts("[NK-PROTECTION TEST] Writing to stack ");
    serial_puts(name);
    serial_puts(" at 0x");
    serial_put_hex((uint64_t)addr);
    serial_puts("\n");
    *addr = 0xDEADBEEF;  /* Should fault */
    serial_puts("[NK-PROTECTION TEST] ERROR: Stack write succeeded!\n");
}

/**
 * run_nk_protection_tests - Run all nested kernel mappings protection tests
 * Returns: Never returns (system shuts down on first fault)
 */
int run_nk_protection_tests(void) {
    serial_puts("\n========================================\n");
    serial_puts("NESTED KERNEL PROTECTION TESTS\n");
    serial_puts("========================================\n");

    /* Verify unprivileged mode */
    uint64_t current_cr3;
    asm volatile ("mov %%cr3, %0" : "=r"(current_cr3));

    if (current_cr3 != unpriv_pml4_phys) {
        serial_puts("NK-PROTECTION TEST: ERROR - Not in unprivileged mode\n");
        return -1;
    }
    serial_puts("NK-PROTECTION TEST: Running in UNPRIVILEGED mode\n");

    /* ============================================================
     * Test 1: Outer kernel writing to page table pages
     * Expected: Critical page fault → system shutdown
     * ============================================================ */
    serial_puts("\n--- Test 1: Write to boot PML4 (page table) ---\n");
    test_write_pagetable("boot PML4", boot_pml4);

    /* ============================================================
     * Test 2: Outer kernel writing to nested kernel code segment
     * Expected: Critical page fault → system shutdown
     * ============================================================ */
    serial_puts("\n--- Test 2: Write to nested kernel code segment ---\n");
    test_write_code_segment("kernel_main", (void *)kernel_main);

    /* ============================================================
     * Test 3: Outer kernel writing to nested kernel data segment
     * Expected: Critical page fault → system shutdown
     * ============================================================ */
    serial_puts("\n--- Test 3: Write to nested kernel data segment ---\n");
    test_write_data_segment("monitor_pml4_phys (data)", (void *)&monitor_pml4_phys);

    /* ============================================================
     * Test 4: Outer kernel writing to nested kernel boot stack
     * Expected: Critical page fault → system shutdown
     * ============================================================ */
    serial_puts("\n--- Test 4: Write to nested kernel boot stack ---\n");
    volatile uint64_t *stack_addr = (uint64_t *)nk_boot_stack_bottom;
    test_write_stack("nk_boot_stack", stack_addr);

    /* If we reach here, all protection tests failed */
    serial_puts("\n========================================\n");
    serial_puts("NK-PROTECTION TEST: FAILED\n");
    serial_puts("All tests passed without triggering faults!\n");
    serial_puts("Nested kernel protection is NOT working.\n");
    serial_puts("========================================\n");
    return -1;
}
