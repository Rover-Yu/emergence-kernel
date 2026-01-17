/* JAKernel - x86-64 IPI (Inter-Processor Interrupt) Driver */

#ifndef JAKERNEL_ARCH_X86_64_IPI_H
#define JAKERNEL_ARCH_X86_64_IPI_H

#include <stdint.h>
#include <stdbool.h>

/* IPI device match ID */
#define IPI_DEVICE_MATCH_ID    0x4950  /* "IP" in hex */

/* IPI vector numbers for testing */
#define IPI_TEST_VECTOR        33      /* IPI test interrupt vector */

/* IPI driver initialization function */
int ipi_driver_init(void);

/* Send IPI to current CPU (self-IPI for testing) */
void ipi_send_self(void);

/* Send IPI to specified CPU by APIC ID */
int ipi_send_to_cpu(uint8_t apic_id);

/* Send IPI to CPU index (0-3) */
int ipi_send_to_cpu_index(int cpu_index);

/* Get current CPU's APIC ID */
uint8_t ipi_get_current_apic_id(void);

/* Check if current CPU is BSP */
bool ipi_is_bsp(void);

/* IPI test function - sends 3 self-IPIs with math expressions */
void ipi_test_start(void);

#endif /* JAKERNEL_ARCH_X86_64_IPI_H */
