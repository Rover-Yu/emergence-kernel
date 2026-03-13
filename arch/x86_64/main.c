/* Emergence Kernel - x86_64 architecture-specific kernel main */

#include <stdint.h>
#include "kernel/device.h"
#include "arch/x86_64/smp.h"
#include "kernel/pmm.h"
#include "arch/x86_64/vga.h"
#include "arch/x86_64/apic.h"
#include "arch/x86_64/acpi.h"
#include "arch/x86_64/idt.h"
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/cr.h"
#include "arch/x86_64/timer.h"
#include "arch/x86_64/ipi.h"
#include "arch/x86_64/serial.h"
#include "arch/x86_64/io.h"
#include "arch/x86_64/power.h"
#include "arch/x86_64/multiboot2.h"
#include "kernel/test.h"
#include "kernel/klog.h"
#include "arch/x86_64/include/syscall.h"

/* Test wrapper headers */
#include "tests/testcases.h"

/* Forward declarations for thread, scheduler, and process */
extern void thread_init(void);
extern void scheduler_init(void);
extern void process_init(void);

/* External monitor functions */
extern void monitor_init(void);
extern uint64_t monitor_get_unpriv_cr3(void);
extern uint64_t monitor_pml4_phys;

/* External per-CPU data for GS-base setup */
extern per_cpu_data_t per_cpu_data[];

/* Architecture-independent halt function */
void kernel_halt(void) {
    while (1) {
        arch_halt();
    }
}

/**
 * run_tests - Execute all post-initialization tests
 *
 * Runs tests that require full system initialization including SMP,
 * interrupts, and monitor context. Called only on BSP after APs are ready.
 */
static void run_tests(void) {
    /* Spinlock tests (BSP setup - handles spinlock_test_start flag) */
    test_spinlock_bsp_setup();

    /* APIC Timer Tests - run after APs are ready */
    test_timer();

    /* NK protection tests - manual only */
    test_nk_fault_injection();

    /* Boot tests */
    test_boot();

    /* SMP tests */
    test_smp();

    /* PCD tests */
    test_pcd();

    /* Nested Kernel invariants tests */
    test_nk_invariants();

    /* Read-only visibility tests */
    test_nk_readonly_visibility();

    /* SMP monitor stress tests */
    test_nk_smp_monitor_stress();

    /* Minilibc string library tests */
    test_minilibc();

    /* Run unified tests (includes usermode and syscall tests)
     * These tests may shut down the system, so run them last */
    test_run_unified();

    /* All tests completed - shut down cleanly */
    system_shutdown();
}

/* Kernel main entry point - called from boot.S */
void kernel_main(uint32_t multiboot_info_addr) {
    uint8_t color = VGA_COLOR(VGA_COLOR_BLACK, VGA_COLOR_WHITE);
    int cpu_id;
    int skip_cr3_switch = 0;

    /* Initialize serial driver early for debug output */
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
    vga_init();
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

    /* Initialize Physical Memory Manager (must be early, before any dynamic allocation) */
    pmm_init(multiboot_info_addr);
    klog_info("KERN", "PMM initialized");

    /* Step 3: Initialize all devices in priority order */
    device_init_all();

    /* Parse and display kernel command line */
    multiboot_get_cmdline();

    /* Initialize logging subsystem (after cmdline is available) */
    klog_init();

    /* Initialize test framework (parses test= parameter from cmdline) */
    test_framework_init();

    /* PMM Tests (early - uses pmm_alloc directly) */
    test_pmm_early();

    /* Initialize Slab Allocator (for small object allocation) */
    extern void slab_init(void);
    slab_init();

    /* Initialize Process subsystem (requires slab allocator) */
    process_init();
    klog_info("KERN", "Process subsystem initialized");

    /* Initialize Thread subsystem (requires slab allocator) */
    thread_init();
    klog_info("KERN", "Thread subsystem initialized");

    /* Initialize Page Control Data (PCD) system */
    extern void pcd_init(void);
    pcd_init();

    /* Slab Allocator Tests */
    test_slab();

    /* Initialize SMP subsystem (detects CPU count from ACPI/CPUID) */
    smp_init();

    /* Initialize Scheduler (requires smp_init for CPU count) */
    scheduler_init();
    klog_info("KERN", "Scheduler initialized");

    /* Scheduler Tests */
    extern void test_sched(void);
    test_sched();

    /* BSP specific initialization - must complete BEFORE starting APs */
    /* Get CPU ID first to determine if we're BSP or AP */
    cpu_id = smp_get_cpu_index();

    /* Set GS base for this CPU to point to per_cpu_data */
    smp_set_gs_base(&per_cpu_data[cpu_id]);

    if (cpu_id == 0) {
        klog_info("SMP", "BSP initializing");
        idt_init();
        lapic_init();
        klog_info("SMP", "BSP initialization complete");
    }

    /* Print CPU boot message in English */
    klog_info("SMP", "CPU %d (APIC ID %d) booted successfully", cpu_id, smp_get_apic_id());

    /* BSP specific initialization */
    if (cpu_id == 0) {
        /* Initialize IPI driver */
        ipi_driver_init();

        /* Initialize nested kernel monitor
         * test_usermode_prepare() handles special setup for usermode tests:
         * - Disables monitor init when running usermode test
         * - Pre-allocates user stack before CR3 switch
         */
        klog_info("KERN", "Initializing monitor");
        skip_cr3_switch = test_usermode_prepare();
        if (!skip_cr3_switch) {
            monitor_init();
        }

        /* Load shared page table with CR0.WP protection (unless usermode test is running) */
        if (!skip_cr3_switch) {
            uint64_t unpriv_cr3 = monitor_get_unpriv_cr3();
            if (unpriv_cr3 != 0) {
                klog_info("KERN", "Loading shared page table with CR0.WP protection");

                /* Enable write protection enforcement */
                /* Set CR0.WP=1 so outer kernel cannot modify read-only PTEs
                 * The monitor trampoline will toggle CR0.WP for NK/OK transitions:
                 * - NK entry: Clear CR0.WP to allow writes to read-only pages
                 * - NK exit: Restore CR0.WP to enforce protection */
                uint64_t cr0 = arch_cr0_read();
                cr0 |= (1 << 16);  /* Set CR0.WP bit */
                arch_cr0_write(cr0);
                klog_info("KERN", "CR0.WP enabled (write protection enforced)");

                /* Load shared page table (unpriv_pml4) with PTPs marked read-only
                 * This single page table is used for both NK and OK modes
                 * Privilege separation is via CR0.WP toggle, not CR3 switch */
                arch_cr3_write(unpriv_cr3);
                klog_info("KERN", "Shared page table loaded");

                /* Verify NK invariants after CR3 switch */
                test_nk_invariants_verify();

#if CONFIG_DEBUG_PCD_STATS
                /* Dump PCD statistics for debugging */
                extern void pcd_dump_stats(void);
                pcd_dump_stats();
#endif

                /* PMM Tests via monitor (uses monitor_pmm_alloc) */
                test_pmm_via_monitor();

                /* Monitor trampoline test */
                test_nk_monitor_trampoline();
            } else {
                klog_error("KERN", "Monitor initialization failed");
            }
        } else {
            /* Usermode tests need full access for ring 3 transition testing */
            klog_info("KERN", "Skipping CR3 switch for usermode tests");
        }

        /* Enable interrupts */
        enable_interrupts_raw();

        /* Disable interrupts for AP startup */
        disable_interrupts();

        /* Mark BSP as ready */
        smp_mark_cpu_ready(0);

        /* Start all APs */
        klog_info("SMP", "Starting APs");
        serial_unlock();
        smp_start_all_aps();

        /* Re-enable interrupts after AP startup to allow APIC timer to fire
         * This allows the APIC timer to generate interrupts */
        enable_interrupts();

        /* Run all post-initialization tests */
        run_tests();

        /* BSP waits with interrupts enabled for timer interrupts to fire
         * The HLT instruction will wake up on each interrupt */
        system_shutdown();
    } else {
        /* AP: print boot message and halt */
        klog_info("SMP", "AP %d initialization complete", cpu_id);

        /* Mark CPU as ready */
        smp_mark_cpu_ready(cpu_id);

        /* Halt the AP */
        kernel_halt();
    }
}
