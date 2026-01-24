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
    return SMP_MAX_CPUS;  /* Configurable in smp.h */
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

    /* Start each AP using STARTUP IPI */
    for (int i = 1; i < SMP_MAX_CPUS; i++) {
        uint8_t apic_id = smp_get_apic_id_by_index(i);

        /* Minimal output before AP startup - don't interfere with AP debug */
        serial_puts("SMP: Starting AP ");
        serial_putc('0' + i);
        serial_puts("...\n");

        /* Mark AP as booting */
        cpu_info[i].state = CPU_BOOTING;

        /* Send STARTUP IPI to AP at physical address 0x7000 */
        int ret = ap_startup(apic_id, TRAMPOLINE_PAGE);

        if (ret < 0) {
            serial_puts("SMP: AP ");
            serial_putc('0' + i);
            serial_puts(" startup FAILED!\n");
            cpu_info[i].state = CPU_OFFLINE;
        }
        /* No success output here - ap_startup() already outputs */
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

    /* Output "JIMI" to complete "HAJIMI" (trampoline outputs "HA") */
    serial_puts("JIMI");

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
 * with the correct addresses for boot_pml4, ap_start, and GDT base addresses.
 *
 * The new trampoline format uses:
 * - gdt32_base_byte: Byte at offset (trampoline_size - 4) - GDT32 base >> 8
 * - gdt64_base_byte: Byte at offset (trampoline_size - 3) - GDT64 base >> 8
 * - boot_pml4_value: 64-bit value at offset (trampoline_size - 24)
 * - ap_start_value: 64-bit value at offset (trampoline_size - 16)
 */
void patch_ap_trampoline(void) {
    extern void serial_puts(const char *str);
    extern void serial_putc(char c);

    /* Copy trampoline to 0x7000 manually FIRST */
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
    /* Check for FA (cli) followed by BA F8 03 (mov dx, 0x3F8) */
    if (tramp_check[0] == 0xFA && tramp_check[1] == 0xBA && tramp_check[2] == 0xF8) {
        serial_puts("SMP: Trampoline signature OK\n");
    } else {
        serial_puts("SMP: ERROR - Trampoline signature mismatch!\n");
    }

    uint8_t *trampoline = (uint8_t *)0x7000;

    /* ============================================================
     * Find GDT32 and GDT64 offsets by searching for their patterns
     * ============================================================
     * GDT32: null quad + 32-bit code + 32-bit data
     * GDT64: null quad + 64-bit code + 64-bit data
     */
    uint32_t gdt32_offset = 0;
    uint32_t gdt64_offset = 0;

    serial_puts("SMP: Searching for GDTs...\n");

    for (uint32_t i = 0x80; i < trampoline_size - 32; i++) {
        /* GDT32 pattern: [null 8 bytes][code: FF FF 00 00 00 9A CF][data: FF FF 00 00 00 92 CF] */
        if (i + 24 < trampoline_size &&
            trampoline[i] == 0x00 && trampoline[i+1] == 0x00 &&
            trampoline[i+2] == 0x00 && trampoline[i+3] == 0x00 &&
            trampoline[i+4] == 0x00 && trampoline[i+5] == 0x00 &&
            trampoline[i+6] == 0x00 && trampoline[i+7] == 0x00 &&
            trampoline[i+8] == 0xFF && trampoline[i+9] == 0xFF &&
            trampoline[i+10] == 0x00 && trampoline[i+11] == 0x00 &&
            trampoline[i+12] == 0x00 && trampoline[i+13] == 0x9A &&
            trampoline[i+14] == 0xCF &&
            trampoline[i+16] == 0xFF && trampoline[i+17] == 0xFF &&
            trampoline[i+18] == 0x00 && trampoline[i+19] == 0x00 &&
            trampoline[i+20] == 0x00 && trampoline[i+21] == 0x92 &&
            trampoline[i+22] == 0xCF) {
            gdt32_offset = i;
            serial_puts("SMP: Found GDT32 at offset ");
            if (i >= 100) serial_putc('0' + (i / 100));
            if (i >= 10) serial_putc('0' + ((i / 10) % 10));
            serial_putc('0' + (i % 10));
            serial_puts("\n");
        }

        /* GDT64 pattern: [null 8 bytes][code: 00 00 00 00 00 9A 20][data: 00 00 00 00 00 92 00] */
        if (i + 24 < trampoline_size &&
            trampoline[i] == 0x00 && trampoline[i+1] == 0x00 &&
            trampoline[i+2] == 0x00 && trampoline[i+3] == 0x00 &&
            trampoline[i+4] == 0x00 && trampoline[i+5] == 0x00 &&
            trampoline[i+6] == 0x00 && trampoline[i+7] == 0x00 &&
            trampoline[i+8] == 0x00 && trampoline[i+9] == 0x00 &&
            trampoline[i+10] == 0x00 && trampoline[i+11] == 0x00 &&
            trampoline[i+12] == 0x00 && trampoline[i+13] == 0x00 &&
            trampoline[i+14] == 0x9A && trampoline[i+15] == 0x20 &&
            trampoline[i+16] == 0x00 && trampoline[i+17] == 0x00 &&
            trampoline[i+18] == 0x00 && trampoline[i+19] == 0x00 &&
            trampoline[i+20] == 0x00 && trampoline[i+21] == 0x92 &&
            trampoline[i+22] == 0x00) {
            gdt64_offset = i;
            serial_puts("SMP: Found GDT64 at offset ");
            if (i >= 100) serial_putc('0' + (i / 100));
            if (i >= 10) serial_putc('0' + ((i / 10) % 10));
            serial_putc('0' + (i % 10));
            serial_puts("\n");
        }
    }

    /* ============================================================
     * Patch the GDT base bytes in the GDTR setup code
     * ============================================================
     * The trampoline stores GDTR at fixed locations in memory
     * We need to patch byte 2 of the base address
     *
     * For GDT32: GDTR is at 0x7BE0, byte 2 (offset 2 from 0x7BE0) needs patching
     * For GDT64: GDTR is at 0x7BF0, byte 2 (offset 2 from 0x7BF0) needs patching
     */

    /* Calculate the byte 2 values for GDT base addresses
     * Base = 0x7000 + gdt_offset
     * Byte 2 = (Base >> 8) & 0xFF = 0x70 + (gdt_offset >> 8)
     */

    if (gdt32_offset > 0) {
        /* Calculate the full GDT32 base address = 0x7000 + gdt32_offset */
        uint32_t gdt32_base = 0x7000 + gdt32_offset;

        /* GDT32 GDTR is at 0x7BE0 in trampoline
         * GDTR format: [limit 16-bit] [base 32-bit little-endian]
         * Bytes 0-1: limit
         * Bytes 2-5: base (byte 2 = base[7:0], byte 3 = base[15:8], etc.)
         */
        uint8_t *gdt32_gdtr = (uint8_t *)0x7BE0;
        gdt32_gdtr[2] = gdt32_base & 0xFF;          /* Base 7:0 */
        gdt32_gdtr[3] = (gdt32_base >> 8) & 0xFF;  /* Base 15:8 */
        gdt32_gdtr[4] = (gdt32_base >> 16) & 0xFF; /* Base 23:16 */
        gdt32_gdtr[5] = (gdt32_base >> 24) & 0xFF; /* Base 31:24 */

        serial_puts("SMP: Patched GDT32 GDTR base to 0x");
        for (int i = 28; i >= 0; i -= 4) {
            uint8_t nibble = (gdt32_base >> i) & 0xF;
            serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
        }
        serial_puts("\n");

        /* Also patch the GDT32 code descriptor base to 0x7000
         * The code descriptor is at gdt32_offset + 8 (second descriptor)
         * GDT descriptor format for 32-bit code:
         *   [0-1]: Limit 15:00
         *   [2]: Base 7:0
         *   [3]: Base 15:8 <- This needs to be 0x70 for 0x7000 base
         *   [4]: Base 23:16
         *   [5]: Access byte
         *   [6]: Flags + Limit 19:16
         *   [7]: Base 31:24 <- This is 0x00 for 0x7000 base
         */
        uint8_t *gdt32_code_desc = (uint8_t *)0x7000 + gdt32_offset + 8;
        gdt32_code_desc[2] = 0x00;  /* Base 7:0 = 0x00 */
        gdt32_code_desc[3] = 0x70;  /* Base 15:8 = 0x70 */
        gdt32_code_desc[4] = 0x00;  /* Base 23:16 = 0x00 */
        gdt32_code_desc[7] = 0x00;  /* Base 31:24 = 0x00 */
        serial_puts("SMP: Patched GDT32 code descriptor base to 0x7000\n");
        /* Verify the descriptor bytes */
        serial_puts("SMP: Code descriptor bytes: ");
        for (int i = 0; i < 8; i++) {
            serial_putc('0' + ((gdt32_code_desc[i] >> 4) & 0xF));
            serial_putc('0' + (gdt32_code_desc[i] & 0xF));
            serial_putc(' ');
        }
        serial_puts("\n");

        uint8_t *gdt32_data_desc = (uint8_t *)0x7000 + gdt32_offset + 16;
        gdt32_data_desc[2] = 0x00;  /* Base 7:0 = 0x00 */
        gdt32_data_desc[3] = 0x70;  /* Base 15:8 = 0x70 */
        gdt32_data_desc[4] = 0x00;  /* Base 23:16 = 0x00 */
        gdt32_data_desc[7] = 0x00;  /* Base 31:24 = 0x00 */
        serial_puts("SMP: Patched GDT32 data descriptor base to 0x7000\n");
    } else {
        serial_puts("SMP: WARNING - GDT32 not found, using default offset\n");
        /* Fallback: GDT32 should be at offset 0x110 (272) due to .align 16 */
        uint32_t gdt32_base = 0x7000 + 272;
        uint8_t *gdt32_gdtr = (uint8_t *)0x7BE0;
        gdt32_gdtr[2] = gdt32_base & 0xFF;
        gdt32_gdtr[3] = (gdt32_base >> 8) & 0xFF;
        gdt32_gdtr[4] = (gdt32_base >> 16) & 0xFF;
        gdt32_gdtr[5] = (gdt32_base >> 24) & 0xFF;
        uint8_t *gdt32_code_desc = (uint8_t *)0x7000 + 256 + 8;
        gdt32_code_desc[2] = 0x00;  /* Base 7:0 = 0x00 */
        gdt32_code_desc[3] = 0x70;  /* Base 15:8 = 0x70 */
        gdt32_code_desc[4] = 0x00;  /* Base 23:16 = 0x00 */
        gdt32_code_desc[7] = 0x00;  /* Base 31:24 = 0x00 */
        uint8_t *gdt32_data_desc = (uint8_t *)0x7000 + 256 + 16;
        gdt32_data_desc[2] = 0x00;  /* Base 7:0 = 0x00 */
        gdt32_data_desc[3] = 0x70;  /* Base 15:8 = 0x70 */
        gdt32_data_desc[4] = 0x00;  /* Base 23:16 = 0x00 */
        gdt32_data_desc[7] = 0x00;  /* Base 31:24 = 0x00 */
    }

    if (gdt64_offset > 0) {
        /* Calculate the full GDT64 base address = 0x7000 + gdt64_offset */
        uint32_t gdt64_base = 0x7000 + gdt64_offset;

        /* GDT64 GDTR is at 0x7BF0 in trampoline
         * GDTR format: [limit 16-bit] [base 32-bit little-endian]
         */
        uint8_t *gdt64_gdtr = (uint8_t *)0x7BF0;
        gdt64_gdtr[2] = gdt64_base & 0xFF;
        gdt64_gdtr[3] = (gdt64_base >> 8) & 0xFF;
        gdt64_gdtr[4] = (gdt64_base >> 16) & 0xFF;
        gdt64_gdtr[5] = (gdt64_base >> 24) & 0xFF;

        serial_puts("SMP: Patched GDT64 GDTR base to 0x");
        for (int i = 28; i >= 0; i -= 4) {
            uint8_t nibble = (gdt64_base >> i) & 0xF;
            serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
        }
        serial_puts("\n");
    }

    /* ============================================================
     * Patch boot_pml4_value and ap_start_value at end of trampoline
     * ============================================================
     * In the new format:
     * - boot_pml4_value is at trampoline_size - 24
     * - ap_start_value is at trampoline_size - 16
     */

    serial_puts("SMP: Patching boot_pml4_value...\n");
    uint64_t *boot_pml4_ptr = (uint64_t *)(trampoline + trampoline_size - 24);
    *boot_pml4_ptr = (uint64_t)&boot_pml4;

    serial_puts("SMP: Patching ap_start_value...\n");
    uint64_t *ap_start_ptr = (uint64_t *)(trampoline + trampoline_size - 16);
    *ap_start_ptr = (uint64_t)&ap_start;

    /* Verify patching worked */
    serial_puts("SMP: Verifying patched values:\n");
    serial_puts("SMP:   boot_pml4_value = 0x");
    uint64_t verify_pml4 = *boot_pml4_ptr;
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t byte = (verify_pml4 >> i) & 0xF;
        serial_putc(byte < 10 ? '0' + byte : 'A' + byte - 10);
    }
    serial_puts("\n");
    serial_puts("SMP:   ap_start_value = 0x");
    uint64_t verify_ap = *ap_start_ptr;
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t byte = (verify_ap >> i) & 0xF;
        serial_putc(byte < 10 ? '0' + byte : 'A' + byte - 10);
    }
    serial_puts("\n");

    serial_puts("SMP: Trampoline ready at 0x7000\n");
}
