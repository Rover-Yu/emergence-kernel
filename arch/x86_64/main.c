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
#include "arch/x86_64/multiboot2.h"
#include "kernel/test.h"
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

    /* DEBUG: Print received multiboot info address */
    serial_puts("MAIN: mbi_addr=0x");
    serial_put_hex(multiboot_info_addr);
    serial_puts("\n");

    /* Check if the address is zero - this indicates multiboot didn't provide info */
    if (multiboot_info_addr == 0) {
        serial_puts("MAIN: WARNING: mbi_addr is 0, multiboot may not have loaded properly\n");
    }

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

    /* Parse and display kernel command line */
    multiboot_get_cmdline();

    /* Initialize test framework (parses test= parameter from cmdline) */
    test_framework_init();

#if CONFIG_TESTS_PMM
    /* PMM Tests */
    if (test_should_run("pmm")) {
        test_run_by_name("pmm");
    }
#endif /* CONFIG_PMM_TESTS */

    /* Initialize Slab Allocator (for small object allocation) */
    extern void slab_init(void);
    slab_init();

    /* Initialize Page Control Data (PCD) system */
    extern void pcd_init(void);
    pcd_init();

#if CONFIG_TESTS_SLAB
    /* Slab Allocator Tests */
    if (test_should_run("slab")) {
        test_run_by_name("slab");
    }
#endif /* CONFIG_SLAB_TESTS */

    /* BSP specific initialization - must complete BEFORE starting APs */
    /* Get CPU ID first to determine if we're BSP or AP */
    cpu_id = smp_get_cpu_index();

    if (cpu_id == 0) {
        serial_puts("BSP: Initializing...\n");
        idt_init();
        lapic_init();
        /* smp_init() calls smp_start_all_aps() which starts APs
         * This is WRONG - smp_init should only be called at top level, not here
         * Remove smp_init() call from BSP block - APs will be started by main() */        serial_puts("BSP: Initialization complete\n");
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

        /* Initialize nested kernel monitor
         * Only disable monitor for usermode tests to allow ring 3 transition */
        serial_puts("KERNEL: Initializing monitor...\n");
#if CONFIG_TESTS_USERMODE
        /* Check if usermode test is specifically requested */
        extern const char *cmdline_get_value(const char *key);
        const char *test_value = cmdline_get_value("test");

        /* Simple string comparison */
        int is_usermode = 0;
        if (test_value) {
            const char *p = test_value;
            const char *u = "usermode";
            while (*u && *p && *p == *u) {
                p++; u++;
            }
            if (*u == '\0' && (*p == '\0' || *p == ' ')) {
                is_usermode = 1;
            }
        }

        if (is_usermode) {
            serial_puts("KERNEL: MONITOR DISABLED for ring 3 usermode test\n");
        } else {
            monitor_init();
        }
#else
        monitor_init();
#endif

        /* Pre-allocate user stack BEFORE switching page tables
         * PMM is only accessible with boot page tables */
#if CONFIG_TESTS_USERMODE
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

#if CONFIG_TESTS_NK_INVARIANTS_VERIFY
            /* Verify all Nested Kernel invariants (including CR0.WP) */
            monitor_verify_invariants();
#endif

#if CONFIG_DEBUG_PCD_STATS
            /* Dump PCD statistics for debugging */
            extern void pcd_dump_stats(void);
            pcd_dump_stats();
#endif

#if CONFIG_TESTS_PMM
            /* PMM Tests - now running after monitor initialization
             * Uses monitor_pmm_alloc() for proper PCD enforcement */
            serial_puts("[ PMM tests ] Running allocation tests (via monitor)...\n");

            extern void *monitor_pmm_alloc(uint8_t order);
            extern void monitor_pmm_free(void *addr, uint8_t order);

            /* DEBUG: Before first allocation */
            serial_puts("[ PMM tests ] About to call monitor_pmm_alloc(0)...\n");

            /* Test 1: Single page allocation */
            void *page1 = monitor_pmm_alloc(0);

            serial_puts("[ PMM tests ] First alloc returned, page1 = 0x");
            extern void serial_put_hex(uint64_t value);
            serial_put_hex((uint64_t)page1);
            serial_puts("\n");

            void *page2 = monitor_pmm_alloc(0);
            serial_puts("[ PMM tests ] Allocated page1 at 0x");
            serial_put_hex((uint64_t)page1);
            serial_puts(", page2 at 0x");
            serial_put_hex((uint64_t)page2);
            serial_puts("\n");

            /* Test 2: Multi-page allocation (order 3 = 8 pages = 32KB) */
            void *block = monitor_pmm_alloc(3);
            serial_puts("[ PMM tests ] Allocated 32KB block at 0x");
            serial_put_hex((uint64_t)block);
            serial_puts("\n");

            /* Test 3: Free and coalesce */
            monitor_pmm_free(page1, 0);
            monitor_pmm_free(page2, 0);
            serial_puts("[ PMM tests ] Freed pages (buddy coalescing)\n");

            /* Test 4: Statistics */
            extern uint64_t pmm_get_free_pages(void);
            extern uint64_t pmm_get_total_pages(void);
            uint64_t free = pmm_get_free_pages();
            uint64_t total = pmm_get_total_pages();
            serial_puts("[ PMM tests ] Free: ");
            serial_put_hex(free);
            serial_puts(" / Total: ");
            serial_put_hex(total);
            serial_puts("\n");

            /* Test 5: Allocate adjacent pages to verify they were coalesced */
            void *page3 = monitor_pmm_alloc(1);  /* Request 2 pages */
            serial_puts("[ PMM tests ] Allocated 2-page block at 0x");
            serial_put_hex((uint64_t)page3);
            serial_puts(" (should be same as page1 if coalesced)\n");

            serial_puts("[ PMM tests ] Tests complete\n");
#endif /* CONFIG_PMM_TESTS */

#if CONFIG_NK_TRAMPOLINE_TEST
            /* Test monitor trampoline CR3 switching */
            serial_puts("KERNEL: Testing monitor trampoline...\n");
            extern void test_monitor_call_from_unprivileged(void);
            test_monitor_call_from_unprivileged();
#endif /* CONFIG_NK_TRAMPOLINE_TEST */
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

#if CONFIG_TESTS_SPINLOCK
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

#if CONFIG_TESTS_SPINLOCK
        /* Spin lock tests - only run if selected via cmdline */
        if (test_should_run("spinlock")) {
            /* Enable spin lock test mode - APs are polling for this flag
             * Once set, APs will wake from their polling loop and join SMP tests */
            extern volatile int spinlock_test_start;
            spinlock_test_start = 1;

            /* Memory barrier to ensure APs see the flag change immediately */
            asm volatile("" ::: "memory");

            /* Small delay to ensure APs wake up and enter spinlock_test_ap_entry()
             * This gives APs time to exit their polling loop and start waiting for phase 1 */
            for (volatile int i = 0; i < 1000000; i++) {
                asm volatile("pause");
            }

            test_run_by_name("spinlock");
        }
#endif

#if CONFIG_TESTS_APIC_TIMER
        /* APIC Timer Tests - run after APs are ready */
        if (test_should_run("timer")) {
            test_run_by_name("timer");
        }
#endif

#if CONFIG_TESTS_NK_FAULT_INJECTION
        /* NK protection tests - manual only, run if explicitly selected */
        if (test_should_run("nk_protection")) {
            test_run_by_name("nk_protection");  /* Never returns */
        }
#endif

#if CONFIG_TESTS_BOOT
        /* Boot tests - manual only, run if explicitly selected */
        if (test_should_run("boot")) {
            test_run_by_name("boot");
        }
#endif

#if CONFIG_TESTS_SMP
        /* SMP tests - manual only, run if explicitly selected */
        if (test_should_run("smp")) {
            test_run_by_name("smp");
        }
#endif

#if CONFIG_TESTS_PCD
        /* PCD tests - manual only, run if explicitly selected */
        if (test_should_run("pcd")) {
            test_run_by_name("pcd");
        }
#endif

#if CONFIG_TESTS_NK_INVARIANTS
        /* Nested Kernel invariants tests - manual only, run if explicitly selected */
        if (test_should_run("nested_kernel_invariants")) {
            test_run_by_name("nested_kernel_invariants");
        }
#endif

#if CONFIG_TESTS_NK_READONLY_VISIBILITY
        /* Read-only visibility tests - manual only, run if explicitly selected */
        if (test_should_run("readonly_visibility")) {
            test_run_by_name("readonly_visibility");
        }
#endif

#if CONFIG_TESTS_MINILIBC
        /* Minilibc string library tests - auto-run if explicitly selected */
        if (test_should_run("minilibc")) {
            test_run_by_name("minilibc");
        }
#endif

#if CONFIG_TESTS_USERMODE
        /* User mode tests - manual only, run if explicitly selected */
        if (test_should_run("usermode")) {
            test_run_by_name("usermode");
        }
#endif

        /* Run unified tests if test=unified was specified */
        test_run_unified();

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
