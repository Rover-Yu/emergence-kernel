/* Emergence Kernel - x86-64 power management implementation */

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

    /* Flush serial output to ensure all characters are transmitted */
    serial_flush();

    /* Small delay for data propagation through buffers.
     * This is a safety net for file-based serial output, giving the
     * host OS time to flush its buffers before QEMU exits.
     * With file-based serial output, this is typically not needed,
     * but it provides extra reliability for edge cases. */
    for (volatile int i = 0; i < 500000; i++) {
        asm volatile("nop");
    }

    /* Try QEMU/Bochs shutdown port first (8-bit write for predictable exit code) */
    outb(SHUTDOWN_PORT_QEMU, SHUTDOWN_CMD_QEMU);

    /* Halt immediately - QEMU should have exited above.
     * If execution reaches here, QEMU shutdown failed. Continue to fallback below. */
    while (1) {
        asm volatile ("hlt");
    }

    /* The code below should never execute in QEMU. It's only for other environments. */
    outb(SHUTDOWN_PORT_VBOX, SHUTDOWN_CMD_VBOX);
    serial_puts("SHUTDOWN: Port I/O failed, halting...\n");

    /* If still here, halt forever */
    while (1) {
        asm volatile ("hlt");
    }
}
