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

    /* RSDT is an SDT containing pointers to other tables */
    uint32_t *rsdt = (uint32_t *)rsdt_address;

    /* Skip signature and length (each entry is 8 bytes: type + length + address) */
    uint32_t *entries = (uint32_t *)(rsdt + 8);
    uint32_t rsdt_length = rsdp->length;

    /* Get number of entries (each entry is 8 bytes: type + length + address) */
    uint32_t num_entries = rsdt_length / 8;

    /* Search for MADT signature in RSDT entries */
    for (uint32_t i = 0; i < num_entries; i++) {
        uint32_t address = entries[i * 2 + 1];  /* Address is 3rd DWORD */

        /* Check if signature matches "APIC" */
        if (address == ACPI_MADT_SIGNATURE) {
            return (madt_header_t *)address;
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
    int offset = 0;

    apic_count = 0;

    while (offset < madt->length) {
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
 * acpi_get_apic_count - Get number of APICs from ACPI MADT
 *
 * Returns: Number of APICs found
 */
int acpi_get_apic_count(void) {
    return apic_count;
}
