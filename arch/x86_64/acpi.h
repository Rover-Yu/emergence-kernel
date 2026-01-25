/* JAKernel - x86-64 ACPI support */

#ifndef JAKERNEL_ARCH_X86_64_ACPI_H
#define JAKERNEL_ARCH_X86_64_ACPI_H

#include <stdint.h>

/* ACPI signatures as const char arrays */
#define ACPI_RSDP_SIGNATURE    "RSD PTR "
#define ACPI_SDT_SIGNATURE     0x54445353    /* "DMTF" */
#define ACPI_MADT_SIGNATURE     0x43414944    /* "APIC" */

/* MADT entry types */
typedef enum {
    MADT_TYPE_LOCAL_APIC = 0,         /* Processor local APIC */
    /* ... other types ... */
} madt_entry_type_t;

/* RSDP structure - Root System Description Pointer */
typedef struct {
    char signature[8];        /* "RSD PTR " */
    uint8_t checksum;          /* Checksum of entire table */
    char oem_id[6];          /* OEM identifier */
    uint8_t revision;         /* Revision of this structure */
    uint32_t rsdt_address;     /* Physical address of RSDT */
    uint32_t length;           /* Length of RSDT */
} __attribute__((packed)) rsdp_t;

/* MADT header - Multiple APIC Description Table header */
typedef struct {
    uint32_t signature;        /* "APIC" */
    uint32_t length;           /* Length of MADT */
    uint8_t revision;          /* MADT revision (1, 2, 3, 4) */
    uint8_t checksum;          /* Checksum of entire table */
    uint8_t oem_id[6];        /* OEM identifier */
    uint32_t oem_table_id;      /* OEM table identifier */
    uint8_t reserved[3];        /* Reserved */
} __attribute__((packed)) madt_header_t;

/* MADT APIC entry (both local and I/O APIC) */
typedef struct {
    uint8_t type;             /* Entry type (0 or 1) */
    uint8_t length;           /* Length of this entry (8 for local APIC) */
    uint8_t apic_id;           /* Local APIC ID (for Local APIC) */
    uint8_t reserved;          /* Always 0 for Local APIC */
    uint32_t apic_base;        /* Physical address of APIC (for I/O APIC) */
    uint32_t gsi_base;         /* Global System Interrupt base */
} __attribute__((packed)) madt_apic_entry_t;

/* ACPI function prototypes */

/**
 * acpi_find_rsdp - Find RSDP in BIOS memory
 *
 * Searches for RSDP signature in EBDA and BIOS memory range
 *
 * Returns: Pointer to RSDP structure, NULL if not found
 */
rsdp_t *acpi_find_rsdp(void);

/**
 * acpi_find_madt - Find MADT in RSDT
 *
 * Returns: Pointer to MADT header, NULL if not found
 */
madt_header_t *acpi_find_madt(void);

/**
 * acpi_parse_madt - Parse MADT to extract CPU information
 * @madt: Pointer to MADT header
 *
 * Returns: Number of APIC entries found
 */
int acpi_parse_madt(madt_header_t *madt);

/**
 * acpi_get_apic_id - Get Local APIC ID for current CPU
 *
 * Returns: Local APIC ID
 */
uint8_t acpi_get_apic_id(void);

/**
 * acpi_get_apic_count - Get number of APICs from ACPI MADT
 *
 * Returns: Number of APICs found
 */
int acpi_get_apic_count(void);

/**
 * acpi_get_apic_id_by_index - Get APIC ID by index
 * @index: Index into APIC list (0-based)
 *
 * Returns: APIC ID at given index, or 0 if index out of range
 */
uint8_t acpi_get_apic_id_by_index(int index);

#endif /* JAKERNEL_ARCH_X86_64_ACPI_H */
