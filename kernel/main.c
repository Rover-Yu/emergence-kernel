/* JAKernel - Architecture-independent kernel main */

#include <stdint.h>
#include "kernel/device.h"
#include "kernel/smp.h"
#include "arch/x86_64/vga.h"
#include "arch/x86_64/apic.h"
#include "arch/x86_64/acpi.h"
#include "arch/x86_64/idt.h"
#include "arch/x86_64/timer.h"
#include "arch/x86_64/rtc.h"
#include "arch/x86_64/ipi.h"

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

/* Kernel main entry point - called from boot.S */
void kernel_main(void) {
    uint8_t color = VGA_COLOR(VGA_COLOR_BLACK, VGA_COLOR_WHITE);
    int cpu_id;
    int ret;

    /* Initialize VGA directly (only BSP should do this) */
    vga_init();

    /* Initialize device drivers (only BSP) */
    /* Step 1: Register platform-specific drivers and devices */
    ret = serial_driver_init();
    (void)ret;

    /* Step 2: Probe devices and match with drivers */
    ret = device_probe_all();
    (void)ret;

    /* Step 3: Initialize all devices in priority order */
    ret = device_init_all();
    (void)ret;

    /* Print greeting message to VGA */
    vga_puts("Hello, JAKernel!", 0, 0, color);

    /* Print boot message */
    serial_puts("Hello, JAKernel!\n");
    serial_puts("Device driver framework initialized.\n");

    /* Note: ACPI parsing temporarily disabled for testing
     * We'll use default APIC IDs (0, 1, 2, 3) for now */
    serial_puts("Using default APIC ID configuration (4 CPUs)\n");

    /* Note: Local APIC initialization skipped for now
     * QEMU may have APIC enabled by default */
    serial_puts("Initializing SMP...\n");

    /* Initialize SMP subsystem */
    smp_init();

    serial_puts("SMP initialized, getting CPU ID...\n");
    cpu_id = smp_get_cpu_index();

    /* Print CPU boot message in English */
    serial_puts("CPU ");
    serial_putc('0' + cpu_id);
    serial_puts(" (APIC ID ");
    serial_putc('0' + smp_get_apic_id());
    serial_puts("): Successfully booted\n");

    /* BSP specific initialization */
    if (cpu_id == 0) {
        serial_puts("SMP: BSP initialization complete.\n");

        /* Initialize IDT (Interrupt Descriptor Table) */
        serial_puts("Initializing IDT...\n");
        idt_init();

        /* Initialize Local APIC */
        serial_puts("Initializing Local APIC...\n");
        lapic_init();

        /* Initialize and start timer on BSP using RTC (Real Time Clock)
         * RTC can generate periodic interrupts without needing APIC memory mapping
         * We'll use 2Hz (500ms period) for the demo */
        serial_puts("Initializing RTC timer...\n");
        rtc_init(RTC_RATE_2HZ);  /* 2 Hz = 500ms period */
        timer_start();

        /* Initialize IPI driver */
        serial_puts("Initializing IPI driver...\n");
        ret = ipi_driver_init();
        (void)ret;

        serial_puts("Enabling interrupts...\n");
        enable_interrupts();
        serial_puts("Timer started. Quotes will print every 0.5 seconds.\n");

        /* Run IPI self-test (after interrupts are enabled) */
        /* NOTE: IPI test temporarily disabled to debug interrupt issues */
        serial_puts("Skipping IPI test (debugging interrupt issues)...\n");
        /* ipi_test_start(); */

        /* Mark BSP as ready */
        smp_mark_cpu_ready(0);

        /* Patch AP trampoline with correct addresses */
        extern void patch_ap_trampoline(void);
        serial_puts("SMP: Patching AP trampoline...\n");
        patch_ap_trampoline();

        /* Start all APs */
        serial_puts("SMP: Starting all Application Processors...\n");
        smp_start_all_aps();

        serial_puts("SMP: Waiting for all CPUs to be ready...\n");

        /* Wait for all CPUs to be ready */
        smp_wait_for_all_cpus();

        serial_puts("SMP: All CPUs are online and ready\n");
        serial_puts("System initialization complete\n");

        /* Halt the BSP */
        kernel_halt();
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
