/* Emergence Kernel - x86-64 IPI (Inter-Processor Interrupt) Driver */

#ifndef EMERGENCE_ARCH_X86_64_IPI_H
#define EMERGENCE_ARCH_X86_64_IPI_H

#include <stdint.h>
#include <stdbool.h>

/* IPI device match ID */
#define IPI_DEVICE_MATCH_ID    0x4950  /* "IP" in hex */

/* IPI driver initialization function */
int ipi_driver_init(void);

/* IPI interrupt handler (called from ISR) */
void ipi_isr_handler(void);

#endif /* EMERGENCE_ARCH_X86_64_IPI_H */
