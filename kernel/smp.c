/* JAKernel - SMP implementation */

#include <stddef.h>
#include <stdint.h>
#include "kernel/smp.h"
#include "arch/x86_64/apic.h"

/* External ACPI functions for getting APIC information */
extern int acpi_get_apic_count(void);
extern uint8_t acpi_get_apic_id_by_index(int index);

/* Stack area for AP CPUs (aligned to 16 bytes) */
static uint8_t ap_stacks[SMP_MAX_CPUS][CPU_STACK_SIZE] __attribute__((aligned(16)));

/* Per-CPU information */
static smp_cpu_info_t cpu_info[SMP_MAX_CPUS];

/* Number of detected CPUs */
static int cpu_count = 0;

/* Number of CPUs that have completed initialization */
static volatile int ready_cpus = 0;

/* SMP initialization flag - set by BSP after basic init is complete */
static volatile int smp_initialized = 0;

/* CPU ID assignment - atomically incremented by each CPU */
static volatile int next_cpu_id = 0;

/* BSP initialization complete flag - use the same variable as boot.S */
extern volatile int bsp_init_done;
#define bsp_init_complete bsp_init_done  /* Alias for compatibility */

/* Current CPU index (for each CPU) */
static volatile int current_cpu_index = 0;

/**
 * smp_get_cpu_count - Get number of detected CPUs
 *
 * Returns: Number of CPUs
 */
int smp_get_cpu_count(void) {
    return SMP_MAX_CPUS;  /* Always 4 in this implementation */
}

/**
 * smp_get_apic_id - Get current CPU's APIC ID
 *
 * Returns: Local APIC ID from CPU info
 */
uint8_t smp_get_apic_id(void) {
    int idx = smp_get_cpu_index();
    if (idx >= 0 && idx < SMP_MAX_CPUS) {
        return cpu_info[idx].apic_id;
    }
    return 0;
}

/**
 * smp_get_cpu_index - Get current CPU's index
 *
 * Returns: CPU index (0-3)
 */
int smp_get_cpu_index(void) {
    return current_cpu_index;
}

/**
 * smp_get_apic_id_by_index - Get APIC ID by CPU index
 * @cpu_index: CPU index
 *
 * Returns: APIC ID for the given CPU index
 */
uint8_t smp_get_apic_id_by_index(int cpu_index) {
    if (cpu_index >= 0 && cpu_index < SMP_MAX_CPUS) {
        return cpu_info[cpu_index].apic_id;
    }
    return 0;
}

/**
 * smp_get_cpu_info - Get CPU info by index
 * @cpu_index: CPU index
 *
 * Returns: Pointer to CPU info or NULL
 */
smp_cpu_info_t *smp_get_cpu_info(int cpu_index) {
    if (cpu_index < 0 || cpu_index >= SMP_MAX_CPUS) {
        return NULL;
    }
    return &cpu_info[cpu_index];
}

/**
 * smp_mark_cpu_ready - Mark CPU as ready
 * @cpu_index: CPU index
 */
void smp_mark_cpu_ready(int cpu_index) {
    if (cpu_index >= 0 && cpu_index < SMP_MAX_CPUS) {
        cpu_info[cpu_index].state = CPU_READY;
        ready_cpus++;
    }
}

/**
 * smp_wait_for_all_cpus - Wait for all CPUs to be ready
 */
void smp_wait_for_all_cpus(void) {
    while (ready_cpus < SMP_MAX_CPUS) {
        asm volatile ("pause");
    }
}

/**
 * smp_init - Initialize SMP subsystem
 */
void smp_init(void) {
    /* BSP is CPU 0 */
    current_cpu_index = 0;
    next_cpu_id = 1;

    cpu_count = SMP_MAX_CPUS;
    ready_cpus = 0;

    /* Initialize CPU info with real APIC IDs from ACPI */
    for (int i = 0; i < SMP_MAX_CPUS; i++) {
        cpu_info[i].cpu_index = i;
        cpu_info[i].apic_id = acpi_get_apic_id_by_index(i);
        cpu_info[i].state = (i == 0) ? CPU_ONLINE : CPU_OFFLINE;
        cpu_info[i].stack_top = NULL;
    }

    /* Fallback: if ACPI didn't provide APIC IDs, use CPU indices */
    if (cpu_info[0].apic_id == 0 && acpi_get_apic_count() == 0) {
        for (int i = 0; i < SMP_MAX_CPUS; i++) {
            cpu_info[i].apic_id = i;
        }
    }
}

/**
 * smp_start_all_aps - Start all Application Processors
 */
void smp_start_all_aps(void) {
    /* Signal APs that BSP initialization is complete
     * APs will wake up through the normal boot path in boot.S */
    bsp_init_complete = 1;

    /* Delay to allow APs to wake up */
    for (int i = 0; i < 100000; i++) {
        asm volatile ("pause");
    }
}

/**
 * ap_start - Application Processor entry point
 */
void ap_start(void) {
    /* Wait for BSP initialization */
    while (!bsp_init_complete) {
        asm volatile ("pause");
    }

    /* Get CPU ID */
    int my_index = __sync_fetch_and_add(&next_cpu_id, 1);

    if (my_index <= 0 || my_index >= SMP_MAX_CPUS) {
        while (1) { asm volatile ("hlt"); }
    }

    /* Set current CPU index */
    current_cpu_index = my_index;

    /* Set up stack */
    cpu_info[my_index].stack_top = &ap_stacks[my_index][CPU_STACK_SIZE];
    asm volatile ("mov %0, %%rsp" : : "r"(cpu_info[my_index].stack_top));

    cpu_info[my_index].state = CPU_ONLINE;

    /* Call kernel_main for AP initialization path */
    extern void kernel_main(void);
    kernel_main();

    /* Mark CPU as ready (in case kernel_main doesn't halt) */
    smp_mark_cpu_ready(my_index);

    /* Halt */
    while (1) { asm volatile ("hlt"); }
}
