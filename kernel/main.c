/* JAKernel - Architecture-independent kernel main */

#include <stdint.h>
#include "kernel/device.h"
#include "arch/x86_64/vga.h"

/* External driver initialization functions */
extern int serial_driver_init(void);
extern void serial_puts(const char *str);

/* Architecture-independent halt function */
static void kernel_halt(void) {
    while (1) {
        asm volatile ("hlt");
    }
}

/* Kernel main entry point - called from boot.S */
void kernel_main(void) {
    uint8_t color = VGA_COLOR(VGA_COLOR_BLACK, VGA_COLOR_WHITE);
    int ret;

    /* Initialize VGA directly (not using device framework yet) */
    vga_init();

    /* Initialize device drivers */
    /* Step 1: Register platform-specific drivers and devices */
    ret = serial_driver_init();
    (void)ret;

    /* Step 2: Probe devices and match with drivers */
    ret = device_probe_all();
    (void)ret;  /* TODO: Handle error codes */

    /* Step 3: Initialize all devices in priority order */
    ret = device_init_all();
    (void)ret;  /* TODO: Handle error codes */

    /* Print greeting message to VGA */
    vga_puts("Hello, JAKernel!", 0, 0, color);

    /* Print greeting message to serial port (via driver framework) */
    serial_puts("Hello, JAKernel!\n");
    serial_puts("Device driver framework initialized.\n");

    /* Halt the kernel */
    kernel_halt();
}
