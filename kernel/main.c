/* JAKernel - Architecture-independent kernel main */

#include <stdint.h>
#include "kernel/device.h"
#include "kernel/smp.h"
#include "arch/x86_64/vga.h"
#include "arch/x86_64/apic.h"
#include "arch/x86_64/acpi.h"
#include "arch/x86_64/idt.h"
#include "arch/x86_64/timer.h"
#include "arch/x86_64/ipi.h"
#include "arch/x86_64/serial.h"
#include "arch/x86_64/io.h"

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

    /* Initialize VGA directly (only BSP should do this) */
    vga_init();

    /* Initialize device drivers (only BSP) */
    /* Step 1: Register platform-specific drivers and devices */
    serial_driver_init();

    /* Step 2: Probe devices and match with drivers */
    device_probe_all();

    /* Step 3: Initialize all devices in priority order */
    device_init_all();

    /* Print greeting message to VGA */
    vga_puts("Hello, JAKernel!", 0, 0, color);

    /* Print boot message */
    serial_puts("Hello, JAKernel!\n");

    /* BSP specific initialization - must complete BEFORE starting APs */
    /* Get CPU ID first to determine if we're BSP or AP */
    cpu_id = smp_get_cpu_index();

    if (cpu_id == 0) {
        serial_puts("BSP: Initializing...\n");
        idt_init();
        lapic_init();
        smp_init();

        /* Initialize APIC Timer for high-frequency interrupts */
        apic_timer_init();

        /* Activate APIC timer */
        timer_start();

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

        /* Start all APs */
        serial_puts("SMP: Starting APs...\n");
        serial_unlock();
        smp_start_all_aps();

        /* Re-enable interrupts after AP startup to allow APIC timer to fire
         * This allows the APIC timer to generate interrupts */
        enable_interrupts();

        /* BSP waits with interrupts enabled for timer interrupts to fire
         * The HLT instruction will wake up on each interrupt */
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
