/* JAKernel - x86-64 power management implementation */

#include "arch/x86_64/power.h"
#include "arch/x86_64/io.h"
#include "arch/x86_64/serial.h"

/* Shutdown port addresses for various platforms */
#define SHUTDOWN_PORT_QEMU   0xB004  /* QEMU, Bochs */
#define SHUTDOWN_PORT_BOCHS  0x604   /* Older Bochs */
#define SHUTDOWN_PORT_VBOX   0x4004  /* VirtualBox */

/* Shutdown commands */
/* QEMU isa-debug-exit with iosize=1: exit_code = value & 0xff */
/* For exit code 0, we write 0x00 using 8-bit I/O (outb) */
#define SHUTDOWN_CMD_QEMU    0x00
#define SHUTDOWN_CMD_VBOX    0x34

void system_shutdown(void) {
    /* Print shutdown message and exit QEMU cleanly */
    serial_puts("system is shutting down\n");

    /* Try QEMU/Bochs shutdown port first (8-bit write for predictable exit code) */
    outb(SHUTDOWN_PORT_QEMU, SHUTDOWN_CMD_QEMU);

    /* If we reach here, shutdown failed - try VirtualBox (8-bit write) */
    outb(SHUTDOWN_PORT_VBOX, SHUTDOWN_CMD_VBOX);

    /* If still here, halt forever */
    serial_puts("SHUTDOWN: Port I/O failed, halting...\n");
    while (1) {
        asm volatile ("hlt");
    }
}
