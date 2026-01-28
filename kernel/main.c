/* Emergence Kernel - Architecture-independent kernel main */

#include <stdint.h>
#include "kernel/device.h"
#include "kernel/smp.h"
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

/* External driver initialization functions */
extern int serial_driver_init(void);
extern void serial_puts(const char *str);
extern void serial_putc(char c);

/* Architecture-independent halt function */
static void kernel_halt(void) {
    while (1) {
        asm volatile ("hlt");
    }
}

/* Helper: Print hex value to serial */
static void serial_put_hex(uint64_t value) {
    const char hex_chars[] = "0123456789ABCDEF";
    char buf[17];
    int i;

    if (value == 0) {
        serial_puts("0");
        return;
    }

    for (i = 15; i >= 0; i--) {
        buf[i] = hex_chars[value & 0xF];
        value >>= 4;
        if (value == 0) break;
    }

    while (i < 15 && buf[i] == '0') i++;

    while (i <= 15) {
        serial_putc(buf[i++]);
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
    const char *banner_top    = "+============================================================+";
    const char *banner_msg    = "|  Emergence Kernel - Intelligence through Emergence      |";
    const char *banner_bottom = "+============================================================+";

    /* Print banner to VGA (rows 0-2) */
    vga_puts(banner_top,    0, 0, color);
    vga_puts(banner_msg,    1, 0, color);
    vga_puts(banner_bottom, 2, 0, color);

    /* Print banner to serial console */
    serial_puts("\n");
    serial_puts(banner_top);
    serial_puts("\n");
    serial_puts(banner_msg);
    serial_puts("\n");
    serial_puts(banner_bottom);
    serial_puts("\n\n");

    /* Step 2: Probe devices and match with drivers */
    device_probe_all();

    /* Step 3: Initialize all devices in priority order */
    device_init_all();

    /* Initialize Physical Memory Manager (must be early, before any dynamic allocation) */
    pmm_init(multiboot_info_addr);
    serial_puts("PMM: Initialized\n");

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

    /* BSP specific initialization - must complete BEFORE starting APs */
    /* Get CPU ID first to determine if we're BSP or AP */
    cpu_id = smp_get_cpu_index();

    if (cpu_id == 0) {
        serial_puts("BSP: Initializing...\n");
        idt_init();
        lapic_init();
        smp_init();

        /* Initialize APIC Timer for high-frequency interrupts */
        //apic_timer_init();

        /* Activate APIC timer */
        //timer_start();  /* Disabled: APIC timer tests interfere with spin lock tests */

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
