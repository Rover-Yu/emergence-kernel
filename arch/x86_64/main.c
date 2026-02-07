/* Emergence Kernel - x86_64 architecture-specific kernel main */

#include <stdint.h>
#include "kernel/device.h"
#include "arch/x86_64/smp.h"
#include "kernel/pmm.h"
#include "arch/x86_64/vga.h"
#include "arch/x86_64/apic.h"
#include "arch/x86_64/acpi.h"
#include "arch/x86_64/idt.h"
#include "arch/x86_64/timer.h"
#include "arch/x86_64/ipi.h"
#include "arch/x86_64/serial.h"
#include "arch/x86_64/io.h"
#include "arch/x86_64/power.h"
#include "arch/x86_64/include/syscall.h"

/* External driver initialization functions */
extern int serial_driver_init(void);

/* External monitor functions */
extern void monitor_init(void);
extern uint64_t monitor_get_unpriv_cr3(void);
extern uint64_t monitor_pml4_phys;
extern void monitor_verify_invariants(void);

/* Architecture-independent halt function */
void kernel_halt(void) {
    while (1) {
        asm volatile ("hlt");
    }
}

/* Kernel main entry point - called from boot.S */
void kernel_main(uint32_t multiboot_info_addr) {
    uint8_t color = VGA_COLOR(VGA_COLOR_BLACK, VGA_COLOR_WHITE);
    int cpu_id;

    /* Initialize VGA directly (only BSP should do this) */
    vga_init();

    /* Initialize device drivers (only BSP) */
    /* Step 1: Register platform-specific drivers and devices */
    serial_driver_init();

    /* Print boot banner FIRST - before any other output */
    const char *banner[] = {
        "▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓",
        "░░░                                      ░░░",
        "▓▓▓  [ Emergence Kernel ]  v0.1  ▓▓▓",
        "░░░   > Learning with Every Boot   ░░░",
        "▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓"
    };
    const int banner_lines = sizeof(banner) / sizeof(banner[0]);

    /* Print banner to VGA */
    for (int i = 0; i < banner_lines && i < 25; i++) {
        vga_puts(banner[i], i, 0, color);
    }

    /* Print banner to serial console */
    serial_puts("\n");
    for (int i = 0; i < banner_lines; i++) {
        serial_puts(banner[i]);
        serial_puts("\n");
    }
    serial_puts("\n");

    /* Step 2: Probe devices and match with drivers */
    device_probe_all();

    /* Step 3: Initialize all devices in priority order */
    device_init_all();

    /* Initialize Physical Memory Manager (must be early, before any dynamic allocation) */
    pmm_init(multiboot_info_addr);
    serial_puts("PMM: Initialized\n");

    /* Initialize Page Control Data (PCD) system */
    extern void pcd_init(void);
    pcd_init();

#if CONFIG_PMM_TESTS
    /* PMM Tests */
    serial_puts("[ PMM tests ] Running allocation tests...\n");

    /* Test 1: Single page allocation */
    void *page1 = pmm_alloc(0);
    void *page2 = pmm_alloc(0);
    serial_puts("[ PMM tests ] Allocated page1 at 0x");
    serial_put_hex((uint64_t)page1);
    serial_puts(", page2 at 0x");
    serial_put_hex((uint64_t)page2);
    serial_puts("\n");

    /* Test 2: Multi-page allocation (order 3 = 8 pages = 32KB) */
    void *block = pmm_alloc(3);
    serial_puts("[ PMM tests ] Allocated 32KB block at 0x");
    serial_put_hex((uint64_t)block);
    serial_puts("\n");

    /* Test 3: Free and coalesce */
    pmm_free(page1, 0);
    pmm_free(page2, 0);
    serial_puts("[ PMM tests ] Freed pages (buddy coalescing)\n");

    /* Test 4: Statistics */
    uint64_t free = pmm_get_free_pages();
    uint64_t total = pmm_get_total_pages();
    serial_puts("[ PMM tests ] Free: ");
    serial_put_hex(free);
    serial_puts(" / Total: ");
    serial_put_hex(total);
    serial_puts("\n");

    /* Test 5: Allocate adjacent pages to verify they were coalesced */
    void *page3 = pmm_alloc(1);  /* Request 2 pages */
    serial_puts("[ PMM tests ] Allocated 2-page block at 0x");
    serial_put_hex((uint64_t)page3);
    serial_puts(" (should be same as page1 if coalesced)\n");

    serial_puts("[ PMM tests ] Tests complete\n");
#endif /* CONFIG_PMM_TESTS */

    /* BSP specific initialization - must complete BEFORE starting APs */
    /* Get CPU ID first to determine if we're BSP or AP */
    cpu_id = smp_get_cpu_index();

    if (cpu_id == 0) {
        serial_puts("BSP: Initializing...\n");
        idt_init();
        lapic_init();
        smp_init();

#if CONFIG_APIC_TIMER_TEST
        /* Initialize APIC Timer for high-frequency interrupts */
        apic_timer_init();

        /* Activate APIC timer */
        timer_start();
#endif

        serial_puts("BSP: Initialization complete\n");
    }

    /* Print CPU boot message in English */
    serial_puts("CPU ");
    serial_putc('0' + cpu_id);
    serial_puts(" (APIC ID ");
    serial_putc('0' + smp_get_apic_id());
    serial_puts("): Successfully booted\n");

    /* BSP specific initialization */
    if (cpu_id == 0) {
        /* Initialize IPI driver */
        ipi_driver_init();

        /* Initialize nested kernel monitor */
        serial_puts("KERNEL: Initializing monitor...\n");
#if 0  /* TEMPORARILY DISABLED: Test if monitor interferes with ring 3 transition */
        monitor_init();
#else
        serial_puts("KERNEL: MONITOR DISABLED for ring 3 test\n");
#endif

        /* Pre-allocate user stack BEFORE switching page tables
         * PMM is only accessible with boot page tables */
#if CONFIG_USERMODE_TEST
        extern void *prealloc_user_stack(void);
        void *stack = prealloc_user_stack();
        if (stack) {
            serial_puts("KERNEL: User stack pre-allocated at 0x");
            extern void serial_put_hex(uint64_t);
            serial_put_hex((uint64_t)stack);
            serial_puts("\n");
        }
#endif

        /* TEMPORARY: Skip CR3 switch to test ring 3 with boot page tables (full access)
         * This tests whether the triple fault is caused by nested kernel page tables
         * or by something else (GDT, TSS, syscall mechanism, etc.) */
#if 0
        /* Switch to unprivileged page tables */
        uint64_t unpriv_cr3 = monitor_get_unpriv_cr3();
        if (unpriv_cr3 != 0) {
            serial_puts("KERNEL: Switching to unprivileged mode\n");

            /* Enable write protection enforcement */
            /* Set CR0.WP=1 so outer kernel cannot modify read-only PTEs */
            uint64_t cr0;
            asm volatile ("mov %%cr0, %0" : "=r"(cr0));
            cr0 |= (1 << 16);  /* Set CR0.WP bit */
            asm volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");
            serial_puts("KERNEL: CR0.WP enabled (write protection enforced)\n");

            /* Switch to unprivileged page tables
             * The monitor has set up these tables with proper U/S bits
             * User code pages are marked as user-accessible (U/S=1) */
            asm volatile ("mov %0, %%cr3" : : "r"(unpriv_cr3) : "memory");
            serial_puts("KERNEL: Page table switch complete\n");

            /* Debug: Verify user program page is accessible after CR3 switch */
            extern void user_program_start(void);
            uint64_t user_prog_addr = (uint64_t)user_program_start;
            serial_puts("KERNEL: Verifying user program at 0x");
            extern void serial_put_hex(uint64_t);
            serial_put_hex(user_prog_addr);
            serial_puts("\n");

#if CONFIG_WRITE_PROTECTION_VERIFY
            /* Verify all Nested Kernel invariants (including CR0.WP) */
            monitor_verify_invariants();
#endif

#if CONFIG_PCD_STATS
            /* Dump PCD statistics for debugging */
            extern void pcd_dump_stats(void);
            pcd_dump_stats();
#endif
        } else {
            serial_puts("KERNEL: Monitor initialization failed\n");
        }
#else
        /* SKIPPING CR3 switch for ring 3 test with boot page tables (full access) */
        serial_puts("KERNEL: TEMPORARY - Skipping CR3 switch, using boot page tables for ring 3 test\n");
        serial_puts("KERNEL: This tests if ring 3 transition works with full page table access\n");
#endif

        /* Enable interrupts */
        enable_interrupts();

        /* Disable interrupts for AP startup */
        asm volatile ("cli");

        /* Mark BSP as ready */
        smp_mark_cpu_ready(0);

#if CONFIG_SPINLOCK_TESTS
        /* Keep spinlock_test_start = 0 during AP startup
         * APs will wait for this flag to be set before joining tests */
        extern volatile int spinlock_test_start;
        spinlock_test_start = 0;
#endif

        /* Start all APs */
        serial_puts("SMP: Starting APs...\n");
        serial_unlock();
        smp_start_all_aps();

        /* Re-enable interrupts after AP startup to allow APIC timer to fire
         * This allows the APIC timer to generate interrupts */
        enable_interrupts();

#if CONFIG_SPINLOCK_TESTS
        /* Enable spin lock test mode - APs are polling for this flag
         * Once set, APs will wake from their polling loop and join SMP tests */
        spinlock_test_start = 1;

        /* Memory barrier to ensure APs see the flag change immediately */
        asm volatile("" ::: "memory");

        /* Small delay to ensure APs wake up and enter spinlock_test_ap_entry()
         * This gives APs time to exit their polling loop and start waiting for phase 1 */
        for (volatile int i = 0; i < 1000000; i++) {
            asm volatile("pause");
        }

        /* Run spin lock tests */
        serial_puts("SMP: Starting spin lock tests...\n");
        extern int run_spinlock_tests(void);
        int test_failures = run_spinlock_tests();
        if (test_failures == 0) {
            serial_puts("SMP: All spin lock tests PASSED\n");
        } else {
            serial_puts("SMP: Some spin lock tests FAILED\n");
            serial_puts("SMP: Failures: ");
            serial_put_hex(test_failures);
            serial_puts("\n");
        }
#endif

#if CONFIG_NK_PROTECTION_TESTS
        /* Run nested kernel mappings protection tests - these will trigger faults and shutdown */
        serial_puts("KERNEL: Starting nested kernel mappings protection tests...\n");
        extern int run_nk_protection_tests(void);
        run_nk_protection_tests();  /* Never returns */
        serial_puts("KERNEL: NK protection tests returned unexpectedly\n");
#endif

#if CONFIG_USERMODE_TEST
        /* Run user mode syscall test */
        serial_puts("KERNEL: Starting user mode syscall test...\n");

        /* Initialize syscall support */
        syscall_init();

        /* TEST 1: Verify SYSCALL works from ring 0 (kernel mode) */
        serial_puts("[TEST] SYSCALL from ring 0 (should halt)...\n");
        __asm__ volatile (
            "mov $2, %rax\n"
            "mov $0, %rdi\n"
            "syscall\n"
        );
        /* If we reach here, syscall failed */
        serial_puts("[TEST] ERROR: Syscall returned unexpectedly\n");
        kernel_halt();

        /* TEST 2: Try ring 3 transition */
        serial_puts("[TEST] Ring 3 transition test...\n");
        enter_user_mode();

        /* Should never reach here */
        serial_puts("KERNEL: ERROR: Returned from user mode!\n");
        kernel_halt();
#endif

        /* BSP waits with interrupts enabled for timer interrupts to fire
         * The HLT instruction will wake up on each interrupt */
        system_shutdown();
    } else {
        /* AP: print boot message and halt */
        serial_puts("SMP: AP ");
        serial_putc('0' + cpu_id);
        serial_puts(" initialization complete\n");

        /* Mark CPU as ready */
        smp_mark_cpu_ready(cpu_id);

        /* Halt the AP */
        kernel_halt();
    }
}
