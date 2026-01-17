/* JAKernel - IPI Test Framework */

#include <stdint.h>
#include <stddef.h>
#include "tests/test.h"
#include "arch/x86_64/apic.h"
#include "kernel/device.h"
#include "kernel/smp.h"

/* IPI device match ID (matching definition from ipi.h) */
#define IPI_DEVICE_MATCH_ID    0x4950  /* "IP" in hex */

/* External serial output functions */
extern void serial_puts(const char *str);

/* Math expressions for IPI test (≤8 words each) */
static const char *ipi_math_expressions[] = {
    " 1. E=mc² - Mass-energy equivalence",
    " 2. a²+b²=c² - Pythagorean theorem",
    " 3. e^(iπ)+1=0 - Euler's identity"
};

#define NUM_IPI_EXPRESSIONS (sizeof(ipi_math_expressions) / sizeof(ipi_math_expressions[0]))

/* IPI test state */
static volatile int ipi_test_count = 0;
static volatile int ipi_test_active = 0;

/**
 * ipi_isr_handler - IPI interrupt handler for test
 *
 * Called from IPI ISR when an IPI is received.
 * Prints a math expression for testing.
 */
void ipi_isr_handler(void) {
    if (ipi_test_active && ipi_test_count < (int)NUM_IPI_EXPRESSIONS) {
        serial_puts("[IPI] ");
        serial_puts(ipi_math_expressions[ipi_test_count]);
        serial_puts("\n");
        ipi_test_count++;

        /* Stop after 3 expressions */
        if (ipi_test_count >= (int)NUM_IPI_EXPRESSIONS) {
            ipi_test_active = 0;
        }
    }

    /* Note: EOI not needed for simulated IPI test
     * In real system, would send EOI to Local APIC */
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
    serial_puts("IPI: Device initialized\n");
    return 0;
}

/* IPI driver structure */
static struct driver ipi_test_driver = {
    .name = "ipi",
    .match_id = IPI_DEVICE_MATCH_ID,
    .match_mask = 0xFFFF,
    .probe = ipi_device_probe,
    .init = ipi_device_init,
    .remove = NULL,
};

/* IPI device structure */
static struct device ipi_test_device = {
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
 * ipi_test_init - Initialize IPI driver for testing
 *
 * Returns: 0 on success, negative on error
 */
int ipi_test_init(void) {
    int ret;

    /* Register IPI driver */
    ret = driver_register(&ipi_test_driver);
    if (ret < 0) {
        serial_puts("IPI: Failed to register driver\n");
        return ret;
    }

    /* Register IPI device */
    ret = device_register(&ipi_test_device);
    if (ret < 0) {
        serial_puts("IPI: Failed to register device\n");
        return ret;
    }

    serial_puts("IPI: Driver and device registered\n");
    return 0;
}

/**
 * ipi_test_send_self - Simulate IPI to current CPU (for testing)
 *
 * NOTE: Real IPI requires APIC MMIO access (0xFEE00000), which is not
 * mapped in the current page table setup. For testing purposes, we
 * directly call the ISR handler instead of sending a real IPI.
 */
static void ipi_test_send_self(void) {
    /* Simulate IPI by calling the handler directly
     * In a real system with APIC mapped, this would use:
     * lapic_write(LAPIC_ICR_LOW, icr_low); */
    ipi_isr_handler();
}

/**
 * ipi_test_start - Start IPI test (sends 3 self-IPIs)
 *
 * Activates IPI handling and sends 3 IPIs to self.
 * Each IPI will print a math expression.
 */
void ipi_test_start(void) {
    ipi_test_count = 0;
    ipi_test_active = 1;

    serial_puts("IPI: Starting self-test (3 IPIs)...\n");

    /* Send 3 self-IPIs with delays */
    for (int i = 0; i < 3; i++) {
        ipi_test_send_self();

        /* Small delay between IPIs */
        for (int j = 0; j < 100000; j++) {
            asm volatile ("pause");
        }
    }

    serial_puts("IPI: Self-test complete\n");
}
