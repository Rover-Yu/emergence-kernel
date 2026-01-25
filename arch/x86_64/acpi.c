/* JAKernel - x86-64 ACPI implementation */

#include <stdint.h>
#include <stddef.h>
#include "arch/x86_64/acpi.h"
#include "arch/x86_64/io.h"

/* EBDA (Extended BIOS Data Area) base address */
#define EBDA_BASE 0x9FFFF000  /* Typical EBDA location */

/* BIOS memory range to search for RSDP */
#define BIOS_AREA_START 0x000E0000
#define BIOS_AREA_END   0x000FFFFF

/* RSDP signature string */
#define RSDP_SIGNATURE "RSD PTR "

/* Number of APIC entries found in MADT */
static int apic_count = 0;

/* APIC IDs from MADT (max 4) */
static uint8_t apic_ids[4];

/* Simple memory comparison function */
static int memcmp8(const void *s1, const void *s2, size_t n) {
    const uint8_t *a = s1;
    const uint8_t *b = s2;
    while (n--) {
        if (*a != *b) return (*a < *b) ? -1 : 1;
        a++;
        b++;
    }
    return 0;
}

/**
 * acpi_find_rsdp - Find RSDP in BIOS memory
 *
 * Searches for RSDP signature in:
 * 1. EBDA (first 1KB)
 * 2. BIOS area (0xE0000 - 0xFFFFF)
 *
 * Returns: Pointer to RSDP structure, NULL if not found
 */
rsdp_t *acpi_find_rsdp(void) {
    uint8_t *ptr = (uint8_t *)BIOS_AREA_START;

    /* Search in BIOS area (16-byte aligned) */
    while (ptr < (uint8_t *)BIOS_AREA_END) {
        if (memcmp8(ptr, RSDP_SIGNATURE, 8) == 0) {
            return (rsdp_t *)ptr;
        }
        ptr += 16;  /* RSDP is 16-byte aligned */
    }

    return NULL;
}

/**
 * Helper to read SDT signature at physical address
 */
static uint32_t read_sdt_signature(uint32_t phys_addr) {
    /* SDT signature is the first 4 bytes */
    return *(uint32_t *)(uintptr_t)phys_addr;
}

/**
 * acpi_find_madt - Find MADT in RSDT
 *
 * Returns: Pointer to MADT header, NULL if not found
 */
madt_header_t *acpi_find_madt(void) {
    rsdp_t *rsdp = acpi_find_rsdp();

    if (!rsdp) {
        return NULL;
    }

    /* Get RSDT address */
    uint32_t rsdt_address = rsdp->rsdt_address;

    /* RSDT is an SDT containing pointers to other tables
     * Format: [signature:4][length:4][entry1:4][entry2:4]... */
    uint32_t *rsdt = (uint32_t *)(uintptr_t)rsdt_address;

    /* Length is at offset 4 (second DWORD) */
    uint32_t rsdt_length = rsdt[1];

    /* Entries start at offset 8 (after signature and length)
     * Each entry is a 32-bit physical address to an SDT */
    uint32_t num_entries = (rsdt_length - 8) / 4;
    uint32_t *entries = &rsdt[2];  /* Pointer to first entry */

    /* Search for MADT signature in RSDT entries */
    for (uint32_t i = 0; i < num_entries; i++) {
        uint32_t sdt_address = entries[i];

        /* Read signature at this SDT address */
        uint32_t sig = read_sdt_signature(sdt_address);

        /* Check if signature matches "APIC" (0x43414944 = "APIC" in LE) */
        if (sig == ACPI_MADT_SIGNATURE) {
            return (madt_header_t *)(uintptr_t)sdt_address;
        }
    }

    return NULL;
}

/**
 * acpi_parse_madt - Parse MADT to extract CPU information
 * @madt: Pointer to MADT header
 *
 * Returns: Number of APIC entries found
 */
int acpi_parse_madt(madt_header_t *madt) {
    /* Validate MADT */
    if (madt->signature != ACPI_MADT_SIGNATURE) {
        return 0;
    }

    /* Parse APIC entries */
    uint8_t *entries = (uint8_t *)(madt + 1);
    uint32_t offset = 0;
    uint32_t madt_length = madt->length;

    apic_count = 0;

    while (offset < madt_length) {
        madt_apic_entry_t *entry = (madt_apic_entry_t *)(entries + offset);

        if (entry->type == MADT_TYPE_LOCAL_APIC) {
            /* Local APIC entry */
            if (apic_count < 4) {
                apic_ids[apic_count++] = entry->apic_id;
            }
        }

        offset += entry->length;
    }

    return apic_count;
}

/**
 * acpi_get_apic_id - Get Local APIC ID for current CPU
 *
 * Returns: Local APIC ID (or 0 if ACPI not available)
 */
uint8_t acpi_get_apic_id(void) {
    /* For now, return the first APIC ID from ACPI MADT
     * In a full implementation, this would read from MSR 0x1B */

    if (apic_count > 0) {
        return apic_ids[0];  /* BSP APIC ID */
    }

    return 0;  /* Fallback */
}

/**
 * get_cpu_count_cpuid - Get CPU count using CPUID instruction
 *
 * Uses CPUID leaf 0x01 to get logical processor count from EBX[23:16].
 * This is a hardware-based method that works regardless of ACPI availability.
 *
 * Returns: Number of logical processors in the system
 */
static int get_cpu_count_cpuid(void) {
    uint32_t eax, ebx, ecx, edx;

    /* CPUID leaf 0x01: Processor Info and Feature Bits
     * EBX[23:16] contains the maximum number of addressable IDs for logical processors */
    asm volatile ("cpuid"
                  : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                  : "a"(0x01));

    /* Extract logical processor count from EBX bits 16-23 */
    int count = (ebx >> 16) & 0xFF;

    /* Sanity check: ensure count is valid */
    if (count < 1 || count > 256) {
        return 1;  /* At least 1 CPU (BSP) */
    }

    return count;
}

/**
 * acpi_get_apic_count - Get number of APICs from ACPI MADT
 *
 * Returns: Number of APICs found (using CPUID as fallback if ACPI unavailable)
 */
int acpi_get_apic_count(void) {
    /* If ACPI parsing succeeded, use that count */
    if (apic_count > 0) {
        return apic_count;
    }

    /* Fallback: use CPUID to get actual CPU count from hardware */
    return get_cpu_count_cpuid();
}

/**
 * acpi_get_apic_id_by_index - Get APIC ID by index
 * @index: Index into APIC list (0-based)
 *
 * Returns: APIC ID at given index (using sequential IDs as fallback)
 */
uint8_t acpi_get_apic_id_by_index(int index) {
    if (index >= 0 && index < 4) {
        /* If ACPI parsed successfully, use those APIC IDs */
        if (apic_count > 0 && index < apic_count) {
            return apic_ids[index];
        }
        /* Fallback: use sequential APIC IDs (0, 1, 2, 3) */
        return index;
    }
    return 0;
}
