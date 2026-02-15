/* Emergence Kernel - x86_64 architecture-specific kernel main */

#include <stdint.h>
#include "kernel/device.h"
#include "arch/x86_64/smp.h"
#include "kernel/pmm.h"
#include "arch/x86_64/vga.h"
#include "arch/x86_64/apic.h"
#include "arch/x86_64/acpi.h"
#include "arch/x86_64/idt.h"
#include "arch/x86_64/cr.h"
#include "arch/x86_64/timer.h"
#include "arch/x86_64/ipi.h"
#include "arch/x86_64/serial.h"
#include "arch/x86_64/io.h"
#include "arch/x86_64/power.h"
#include "arch/x86_64/multiboot2.h"
#include "kernel/test.h"
#include "arch/x86_64/include/syscall.h"

/* Test wrapper headers */
#include "tests/pmm/test_pmm.h"
#include "tests/slab/test_slab.h"
#include "tests/spinlock/test_spinlock.h"
#include "tests/timer/test_timer.h"
#include "tests/boot/test_boot.h"
#include "tests/smp/test_smp.h"
#include "tests/pcd/test_pcd.h"
#include "tests/minilibc/test_minilibc.h"
#include "tests/usermode/test_usermode.h"
#include "tests/nested-kernel/test_nk_invariants.h"
#include "tests/nested-kernel/test_nk_readonly_visibility.h"
#include "tests/nested-kernel/test_nk_fault_injection.h"
#include "tests/nested-kernel/test_nk_monitor_trampoline.h"
#include "tests/nested-kernel/test_nk_invariants_verify.h"

/* External driver initialization functions */
extern int serial_driver_init(void);

/* External monitor functions */
extern void monitor_init(void);
extern uint64_t monitor_get_unpriv_cr3(void);
extern uint64_t monitor_pml4_phys;

/* External per-CPU data for GS-base setup */
extern per_cpu_data_t per_cpu_data[];

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
    int skip_cr3_switch = 0;

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

    /* PMM Tests (early - uses pmm_alloc directly) */
    test_pmm_early();

    /* Initialize Slab Allocator (for small object allocation) */
    extern void slab_init(void);
    slab_init();

    /* Initialize Page Control Data (PCD) system */
    extern void pcd_init(void);
    pcd_init();

    /* Slab Allocator Tests */
    test_slab();

    /* BSP specific initialization - must complete BEFORE starting APs */
    /* Get CPU ID first to determine if we're BSP or AP */
    cpu_id = smp_get_cpu_index();

    /* Set GS base for this CPU to point to per_cpu_data */
    smp_set_gs_base(&per_cpu_data[cpu_id]);

    if (cpu_id == 0) {
        serial_puts("BSP: Initializing...\n");
        idt_init();
        lapic_init();
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

        /* Initialize nested kernel monitor
         * test_usermode_prepare() handles special setup for usermode tests:
         * - Disables monitor init when running usermode test
         * - Pre-allocates user stack before CR3 switch
         */
        serial_puts("KERNEL: Initializing monitor...\n");
        skip_cr3_switch = test_usermode_prepare();
        if (!skip_cr3_switch) {
            monitor_init();
        }

        /* Switch to unprivileged page tables (unless usermode test is running) */
        if (!skip_cr3_switch) {
            uint64_t unpriv_cr3 = monitor_get_unpriv_cr3();
            if (unpriv_cr3 != 0) {
                serial_puts("KERNEL: Switching to unprivileged mode\n");

                /* Enable write protection enforcement */
                /* Set CR0.WP=1 so outer kernel cannot modify read-only PTEs */
                uint64_t cr0 = arch_cr0_read();
                cr0 |= (1 << 16);  /* Set CR0.WP bit */
                arch_cr0_write(cr0);
                serial_puts("KERNEL: CR0.WP enabled (write protection enforced)\n");

                /* Switch to unprivileged page tables
                 * The monitor has set up these tables with proper U/S bits
                 * User code pages are marked as user-accessible (U/S=1) */
                arch_cr3_write(unpriv_cr3);
                serial_puts("KERNEL: Page table switch complete\n");

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
                serial_puts("KERNEL: Monitor initialization failed\n");
            }
        } else {
            /* Usermode tests need full access for ring 3 transition testing */
            serial_puts("KERNEL: Skipping CR3 switch for usermode tests\n");
        }

        /* Enable interrupts */
        enable_interrupts_raw();

        /* Disable interrupts for AP startup */
        asm volatile ("cli");

        /* Mark BSP as ready */
        smp_mark_cpu_ready(0);

        /* Start all APs */
        serial_puts("SMP: Starting APs...\n");
        serial_unlock();
        smp_start_all_aps();

        /* Re-enable interrupts after AP startup to allow APIC timer to fire
         * This allows the APIC timer to generate interrupts */
        enable_interrupts();

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

        /* Minilibc string library tests */
        test_minilibc();

        /* User mode tests */
        test_usermode();

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
