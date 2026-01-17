/* JAKernel - Architecture-independent kernel main */

#include <stdint.h>
#include "arch/x86_64/vga.h"
#include "arch/x86_64/serial.h"

/* Architecture-independent halt function */
static void kernel_halt(void) {
    while (1) {
        asm volatile ("hlt");
    }
}

/* Kernel main entry point - called from boot.S */
void kernel_main(void) {
    uint8_t color = VGA_COLOR(VGA_COLOR_BLACK, VGA_COLOR_WHITE);

    /* Initialize hardware devices */
    vga_init();
    serial_init();

    /* Print greeting message to VGA */
    vga_puts("Hello, JAKernel!", 0, 0, color);

    /* Print greeting message to serial port */
    serial_puts("Hello, JAKernel!\n");

    /* Halt the kernel */
    kernel_halt();
}
