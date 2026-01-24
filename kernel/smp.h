/* JAKernel - SMP (Symmetric Multi-Processing) support */

#ifndef JAKERNEL_KERNEL_SMP_H
#define JAKERNEL_KERNEL_SMP_H

#include <stdint.h>
#include "arch/x86_64/apic.h"

/* Maximum number of CPUs supported by the kernel */
#define SMP_MAX_CPUS    4

/* CPU stack size - 16 KiB per CPU for interrupt handling and function calls */
#define CPU_STACK_SIZE  16384  /* 16 KiB = 16 * 1024 bytes */

/* SMP initialization timing constants (in iterations) */
#define SMP_AP_INIT_TIMEOUT      10000000  /* Maximum iterations to wait for AP to initialize (~10ms at 1GHz) */
#define SMP_AP_SETTLE_DELAY       100000   /* Delay iterations after AP completes before starting next (~0.1ms) */

/* CPU states for SMP */
typedef enum {
    CPU_OFFLINE,      /* CPU is offline */
    CPU_BOOTING,      /* CPU is booting */
    CPU_ONLINE,       /* CPU is online and running */
    CPU_READY         /* CPU completed initialization */
} smp_cpu_state_t;

/* Per-CPU information */
typedef struct {
    uint8_t apic_id;           /* Local APIC ID */
    uint8_t cpu_index;         /* CPU index (0, 1, 2, 3) */
    smp_cpu_state_t state;     /* Current state */
    void *stack_top;           /* Top of this CPU's stack */
} smp_cpu_info_t;

/* SMP function prototypes */

/* Initialize SMP subsystem */
void smp_init(void);

/* Get number of detected CPUs */
int smp_get_cpu_count(void);

/* Get current CPU's APIC ID */
uint8_t smp_get_apic_id(void);

/* Get current CPU's index */
int smp_get_cpu_index(void);

/* Get APIC ID by CPU index */
uint8_t smp_get_apic_id_by_index(int cpu_index);

/* Mark CPU as ready */
void smp_mark_cpu_ready(int cpu_index);

/* Wait for all CPUs to be ready */
void smp_wait_for_all_cpus(void);

/* Get CPU info by index */
smp_cpu_info_t *smp_get_cpu_info(int cpu_index);

/* AP (Application Processor) entry point */
void ap_start(void);

/* Start all APs */
void smp_start_all_aps(void);

#endif /* JAKERNEL_KERNEL_SMP_H */
