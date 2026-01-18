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

/* External symbols */
extern void ap_start(void);
extern uint64_t boot_pml4;

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
 *
 * Sends real STARTUP IPIs to all APs via the Local APIC.
 * The AP trampoline is at physical address 0x7000.
 */
void smp_start_all_aps(void) {
    extern int ap_startup(uint8_t apic_id, uint32_t startup_addr);
    extern void serial_puts(const char *str);
    extern void serial_putc(char c);

    /* AP trampoline is at 0x7000 (page 7)
     * Page number for STARTUP IPI = 0x7000 >> 12 = 7 */
    const uint32_t TRAMPOLINE_PAGE = 7;

    serial_puts("SMP: Starting all Application Processors...\n");

    /* Debug: Print CPU info */
    serial_puts("SMP: CPU info:\n");
    for (int i = 0; i < SMP_MAX_CPUS; i++) {
        serial_puts("  CPU ");
        serial_putc('0' + i);
        serial_puts(" -> APIC ID ");
        serial_putc('0' + smp_get_apic_id_by_index(i));
        serial_puts("\n");
    }

    /* Disable interrupts during AP startup to avoid interference */
    serial_puts("SMP: Disabling interrupts for AP startup...\n");
    asm volatile ("cli");

    /* Start each AP (CPU 1, 2, 3) */
    for (int i = 1; i < SMP_MAX_CPUS; i++) {
        uint8_t apic_id = smp_get_apic_id_by_index(i);

        serial_puts("SMP: Starting AP ");
        serial_putc('0' + i);
        serial_puts(" (APIC ID ");
        serial_putc('0' + apic_id);
        serial_puts(")...\n");

        /* Send STARTUP IPI to AP */
        int ret = ap_startup(apic_id, TRAMPOLINE_PAGE);

        if (ret == 0) {
            serial_puts("SMP: STARTUP IPI sent successfully, waiting for AP...\n");
        } else {
            serial_puts("SMP: WARNING - STARTUP IPI failed\n");
        }

        /* Give AP time to start */
        for (int j = 0; j < 10000000; j++) {
            asm volatile ("pause");
        }

        /* Check if AP is ready */
        if (cpu_info[i].state == CPU_READY) {
            serial_puts("SMP: AP ");
            serial_putc('0' + i);
            serial_puts(" is READY!\n");
        } else {
            serial_puts("SMP: AP ");
            serial_putc('0' + i);
            serial_puts(" not ready yet (state=");
            serial_putc('0' + cpu_info[i].state);
            serial_puts(")\n");
        }
    }

    /* Signal APs that BSP initialization is complete */
    bsp_init_complete = 1;
    serial_puts("SMP: bsp_init_complete flag set\n");

    /* Re-enable interrupts */
    serial_puts("SMP: Re-enabling interrupts...\n");
    asm volatile ("sti");

    /* Wait a bit more */
    for (int j = 0; j < 10000000; j++) {
        asm volatile ("pause");
    }

    /* Final check */
    serial_puts("SMP: Final CPU status:\n");
    for (int i = 0; i < SMP_MAX_CPUS; i++) {
        serial_puts("  CPU ");
        serial_putc('0' + i);
        serial_puts(" state=");
        serial_putc('0' + cpu_info[i].state);
        serial_puts("\n");
    }
}

/**
 * ap_start - Application Processor entry point
 */
void ap_start(void) {
    extern void serial_puts(const char *str);
    extern void serial_putc(char c);

    /* Debug output to confirm AP reached this point */
    serial_puts("[AP] ap_start() reached!\n");

    /* Wait for BSP initialization */
    while (!bsp_init_complete) {
        asm volatile ("pause");
    }

    serial_puts("[AP] BSP init complete, getting CPU ID...\n");

    /* Get CPU ID */
    int my_index = __sync_fetch_and_add(&next_cpu_id, 1);

    serial_puts("[AP] Got CPU index: ");
    serial_putc('0' + my_index);
    serial_puts("\n");

    if (my_index <= 0 || my_index >= SMP_MAX_CPUS) {
        serial_puts("[AP] ERROR: Invalid CPU index!\n");
        while (1) { asm volatile ("hlt"); }
    }

    /* Set current CPU index */
    current_cpu_index = my_index;

    /* Set up stack */
    cpu_info[my_index].stack_top = &ap_stacks[my_index][CPU_STACK_SIZE];
    asm volatile ("mov %0, %%rsp" : : "r"(cpu_info[my_index].stack_top));

    cpu_info[my_index].state = CPU_ONLINE;

    serial_puts("[AP] Stack set up, marking CPU as ready...\n");

    /* Mark CPU as ready and halt */
    smp_mark_cpu_ready(my_index);

    serial_puts("[AP] CPU marked as ready, halting...\n");

    /* Halt */
    while (1) { asm volatile ("hlt"); }
}

/**
 * patch_ap_trampoline - Patch the AP trampoline with runtime values
 *
 * This function patches the placeholder values in the AP trampoline
 * with the correct addresses for boot_pml4 and ap_start.
 */
void patch_ap_trampoline(void) {
    extern void serial_puts(const char *str);
    extern void serial_putc(char c);

    /* Trampoline is at physical address 0x7000 */
    uint8_t *trampoline = (uint8_t *)0x7000;

    /* The trampoline ends with:
     * - GDT32 data (24 bytes)
     * - GDT32 ptr (6 bytes)
     * - GDT64 data (24 bytes)
     * - GDT64 ptr (10 bytes with padding)
     * - boot_pml4_placeholder (4 bytes)
     * - ap_start_placeholder (4 bytes)
     *
     * Total from GDT32 to end: 24+6+24+10+4+4 = 72 bytes (0x48)
     *
     * The placeholders are at the very end of the trampoline.
     * We can find them by searching backward from the end.
     */

    /* Trampoline binary is 0xC8 (200) bytes based on the file size
     * Placeholders start at approximately 0xC8 - 8 = 0xC0 */

    uint8_t *ptr = trampoline + 0xC0;  /* Approximate location */

    /* Search backward for the first zero dword (boot_pml4_placeholder = 0) */
    while (ptr > trampoline) {
        uint32_t *dword_ptr = (uint32_t *)ptr;
        if (*dword_ptr == 0) {
            /* Check if next dword is also 0 (ap_start_placeholder) */
            if (*(dword_ptr + 1) == 0) {
                break;
            }
        }
        ptr--;
    }

    if (ptr <= trampoline) {
        serial_puts("SMP: ERROR - Could not find placeholders in trampoline\n");
        return;
    }

    /* Now ptr points to boot_pml4_placeholder */
    uint32_t *boot_pml4_ptr = (uint32_t *)ptr;
    uint32_t *ap_start_ptr = (uint32_t *)(ptr + 4);

    /* Patch boot_pml4 placeholder with low 32 bits of address */
    *boot_pml4_ptr = ((uint32_t)&boot_pml4);

    /* Patch ap_start placeholder with low 32 bits of address */
    *ap_start_ptr = ((uint32_t)&ap_start);

    serial_puts("SMP: Trampoline patched successfully\n");
    serial_puts("SMP: boot_pml4 = 0x");
    uint32_t pml4_addr = (uint32_t)&boot_pml4;
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t byte = (pml4_addr >> i) & 0xF;
        serial_putc(byte < 10 ? '0' + byte : 'A' + byte - 10);
    }
    serial_puts("\n");

    /* Copy trampoline to 0x7000 manually */
    serial_puts("SMP: Copying trampoline to 0x7000...\n");
    extern uint8_t _binary_build_ap_trampoline_bin_start[];
    extern uint8_t _binary_build_ap_trampoline_bin_end[];
    uint8_t *trampoline_src = _binary_build_ap_trampoline_bin_start;
    uint8_t *trampoline_dst = (uint8_t *)0x7000;
    uint32_t trampoline_size = _binary_build_ap_trampoline_bin_end - _binary_build_ap_trampoline_bin_start;

    for (uint32_t i = 0; i < trampoline_size; i++) {
        trampoline_dst[i] = trampoline_src[i];
    }

    /* Verify trampoline is loaded at 0x7000 */
    serial_puts("SMP: Verifying trampoline at 0x7000...\n");
    uint8_t *tramp_check = (uint8_t *)0x7000;
    if (tramp_check[0] == 0xBA && tramp_check[1] == 0xF8) {
        serial_puts("SMP: Trampoline signature OK (mov dx, 0x3F8)\n");
    } else {
        serial_puts("SMP: ERROR - Trampoline signature mismatch!\n");
        serial_puts("SMP: First bytes: ");
        for (int i = 0; i < 16; i++) {
            serial_putc('0' + ((tramp_check[i] >> 4) & 0xF));
            serial_putc('0' + (tramp_check[i] & 0xF));
            serial_putc(' ');
        }
        serial_puts("\n");
    }
}
