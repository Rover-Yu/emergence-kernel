/* Emergence Kernel - x86-64 IPI (Inter-Processor Interrupt) Driver */

#include <stdint.h>
#include <stddef.h>
#include "arch/x86_64/ipi.h"
#include "arch/x86_64/apic.h"
#include "kernel/device.h"
#include "arch/x86_64/smp.h"

/* External functions */
extern void serial_puts(const char *str);
extern void serial_putc(char c);

/* IPI interrupt count */
static volatile int ipi_count = 0;
static volatile int ipi_active = 0;

/**
 * ipi_isr_handler - IPI interrupt handler
 *
 * Called from IPI ISR when an IPI is received.
 * Prints a math expression for testing.
 */
void ipi_isr_handler(void) {
    if (ipi_active && ipi_count < 3) {
        ipi_count++;

        /* Stop after 3 expressions */
        if (ipi_count >= 3) {
            ipi_active = 0;
        }
    }

    /* NOTE: EOI is now sent in the assembly wrapper (isr.S)
     * for consistency with timer_isr and robustness */
}

/**
 * ipi_device_probe - Probe function for IPI device
 * @dev: Device to probe
 *
 * Returns: 0 on success (always matches IPI device)
 */
static int ipi_device_probe(struct device *dev) {
    /* IPI device always matches - it's a platform device */
    (void)dev;
    return 0;
}

/**
 * ipi_device_init - Initialize IPI device
 * @dev: Device to initialize
 *
 * Returns: 0 on success
 */
static int ipi_device_init(struct device *dev) {
    (void)dev;
    return 0;
}

/* IPI driver structure */
static struct driver ipi_driver = {
    .name = "ipi",
    .match_id = IPI_DEVICE_MATCH_ID,
    .match_mask = 0xFFFF,
    .probe = ipi_device_probe,
    .init = ipi_device_init,
};

/* IPI device structure */
static struct device ipi_device = {
    .name = "ipi",
    .type = DEVICE_TYPE_PLATFORM,
    .match_id = IPI_DEVICE_MATCH_ID,
    .init_priority = 10,
    .mmio_base = NULL,
    .mmio_size = 0,
    .io_port_base = 0,
    .io_port_count = 0,
};

/**
 * ipi_driver_init - Initialize IPI driver and register device
 *
 * Returns: 0 on success, negative on error
 */
int ipi_driver_init(void) {
    int ret;

    /* Register IPI driver */
    ret = driver_register(&ipi_driver);
    if (ret < 0) {
        serial_puts("IPI: Failed to register driver\n");
        return ret;
    }

    /* Register IPI device */
    ret = device_register(&ipi_device);
    if (ret < 0) {
        serial_puts("IPI: Failed to register device\n");
        return ret;
    }

    return 0;
}
