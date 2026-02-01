/* Emergence Kernel - Multiboot2 header structure definitions */

#ifndef _KERNEL_MULTIBOOT2_H
#define _KERNEL_MULTIBOOT2_H

#include <stdint.h>

/* Multiboot2 header tag types */
#define MULTIBOOT_TAG_END                  0
#define MULTIBOOT_TAG_CMDLINE              1
#define MULTIBOOT_TAG_BOOT_LOADER_NAME     2
#define MULTIBOOT_TAG_MODULE               3
#define MULTIBOOT_TAG_BASIC_MEMINFO        4
#define MULTIBOOT_TAG_BOOTDEV              5
#define MULTIBOOT_TAG_MMAP                 6
#define MULTIBOOT_TAG_VBE                  7
#define MULTIBOOT_TAG_FRAMEBUFFER          8
#define MULTIBOOT_TAG_ELF_SECTIONS         9
#define MULTIBOOT_TAG_APM                 10
#define MULTIBOOT_TAG_EFI32               11
#define MULTIBOOT_TAG_EFI64               12
#define MULTIBOOT_TAG_SMBIOS              13
#define MULTIBOOT_TAG_ACPI_OLD            14
#define MULTIBOOT_TAG_ACPI_NEW            15
#define MULTIBOOT_TAG_NETWORK             16
#define MULTIBOOT_TAG_EFI_MMAP            17
#define MULTIBOOT_TAG_EFI_BS              18
#define MULTIBOOT_TAG_EFI32_IH            19
#define MULTIBOOT_TAG_EFI64_IH            20
#define MULTIBOOT_TAG_LOAD_BASE_ADDR      21

/* Memory map entry types */
#define MULTIBOOT_MEMORY_AVAILABLE              1
#define MULTIBOOT_MEMORY_RESERVED               2
#define MULTIBOOT_MEMORY_ACPI_RECLAIMABLE       3
#define MULTIBOOT_MEMORY_NVS                    4
#define MULTIBOOT_MEMORY_BADRAM                 5

/* Multiboot2 information structure */
typedef struct multiboot_tag {
    uint32_t type;
    uint32_t size;
} multiboot_tag_t;

/* Memory map entry structure */
typedef struct multiboot_mmap_entry {
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
} multiboot_mmap_entry_t;

/* Memory map tag structure */
typedef struct multiboot_tag_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    multiboot_mmap_entry_t entries[];
} multiboot_tag_mmap_t;

/* Basic memory info tag structure */
typedef struct multiboot_tag_basic_meminfo {
    uint32_t type;
    uint32_t size;
    uint32_t mem_lower;
    uint32_t mem_upper;
} multiboot_tag_basic_meminfo_t;

/* Multiboot2 header structure (for parsing the info structure) */
typedef struct multiboot_info {
    uint32_t total_size;
    uint32_t reserved;
} multiboot_info_t;

/* Function prototypes */
void multiboot2_parse(uint32_t mbi_addr);

#endif /* _KERNEL_MULTIBOOT2_H */
