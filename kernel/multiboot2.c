/* Emergence Kernel - Multiboot2 parser */

#include <stdint.h>
#include "kernel/multiboot2.h"
#include "kernel/pmm.h"
#include "arch/x86_64/serial.h"

/* External function for serial output */
extern void serial_puts(const char *str);
extern void serial_putc(char c);

/* Helper: Convert number to hex string and print */
static void put_hex(uint64_t value) {
    const char hex_chars[] = "0123456789ABCDEF";
    char buf[17];
    int i;

    /* Handle zero */
    if (value == 0) {
        serial_puts("0");
        return;
    }

    /* Convert to hex (reverse) */
    for (i = 15; i >= 0; i--) {
        buf[i] = hex_chars[value & 0xF];
        value >>= 4;
        if (value == 0) break;
    }

    /* Find first non-zero */
    while (i < 15 && buf[i] == '0') i++;

    /* Print */
    while (i <= 15) {
        serial_putc(buf[i++]);
    }
}

/* Parse multiboot2 memory map tag */
static void parse_memory_map(multiboot_tag_mmap_t *tag) {
    multiboot_mmap_entry_t *entry;
    uint64_t total_pages = 0;
    uint64_t usable_pages = 0;
    int i = 0;

    serial_puts("PMM: Parsing memory map\n");

    /* Iterate through memory map entries */
    entry = tag->entries;
    while ((uint8_t *)entry < (uint8_t *)tag + tag->size) {
        uint64_t base = entry->base_addr;
        uint64_t length = entry->length;
        uint32_t type = entry->type;

        if (type == MULTIBOOT_MEMORY_AVAILABLE) {
            /* Align base to page boundary */
            uint64_t aligned_base = (base + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            uint64_t aligned_length = length - (aligned_base - base);

            /* Round down length to page boundary */
            aligned_length = aligned_length & ~(PAGE_SIZE - 1);

            if (aligned_length >= PAGE_SIZE) {
                pmm_add_region(aligned_base, aligned_length);
                usable_pages += aligned_length / PAGE_SIZE;
                serial_puts("PMM: Added region at 0x");
                put_hex(aligned_base);
                serial_puts(", size ");
                put_hex(aligned_length);
                serial_puts(" bytes\n");
            }
        }

        total_pages += (length + PAGE_SIZE - 1) / PAGE_SIZE;

        /* Move to next entry */
        entry = (multiboot_mmap_entry_t *)((uint8_t *)entry + tag->entry_size);
        i++;
    }

    serial_puts("PMM: Total memory: ");
    put_hex(total_pages * PAGE_SIZE);
    serial_puts(" bytes\n");
    serial_puts("PMM: Usable memory: ");
    put_hex(usable_pages * PAGE_SIZE);
    serial_puts(" bytes (");
    put_hex(usable_pages);
    serial_puts(" pages)\n");
}

/* Parse multiboot2 information structure */
void multiboot2_parse(uint32_t mbi_addr) {
    multiboot_info_t *mbi = (multiboot_info_t *)(uint64_t)mbi_addr;
    multiboot_tag_t *tag;
    uint64_t total_size;
    int found_memory = 0;

    serial_puts("PMM: Parsing multiboot2 info at 0x");
    put_hex(mbi_addr);
    serial_puts("\n");

    total_size = mbi->total_size;
    serial_puts("PMM: Total size: ");
    put_hex(total_size);
    serial_puts(" bytes\n");

    /* Iterate through tags */
    tag = (multiboot_tag_t *)(mbi + 1);

    while (tag->type != MULTIBOOT_TAG_END) {
        /* Skip padding to 8-byte alignment */
        if (tag->type == 0 && tag->size == 0) {
            break;
        }

        /* Check for valid tag size */
        if (tag->size < 8 || tag->size > 4096) {
            break;
        }

        switch (tag->type) {
            case MULTIBOOT_TAG_MMAP:
                serial_puts("PMM: Found memory map tag\n");
                parse_memory_map((multiboot_tag_mmap_t *)tag);
                found_memory = 1;
                break;

            case MULTIBOOT_TAG_BASIC_MEMINFO: {
                multiboot_tag_basic_meminfo_t *meminfo =
                    (multiboot_tag_basic_meminfo_t *)tag;
                serial_puts("PMM: Found basic meminfo tag\n");
                serial_puts("PMM: mem_lower=");
                put_hex(meminfo->mem_lower);
                serial_puts("KB, mem_upper=");
                put_hex(meminfo->mem_upper);
                serial_puts("KB\n");

                /* Add memory region from basic meminfo */
                /* mem_lower is lower memory (first 640KB), mem_upper is extended memory */
                uint64_t upper_mem = (uint64_t)meminfo->mem_upper * 1024;
                if (upper_mem > 0) {
                    /* Upper memory starts at 1MB */
                    pmm_add_region(0x100000, upper_mem);
                    found_memory = 1;
                }
                break;
            }

            default:
                /* Ignore other tags */
                break;
        }

        /* Move to next tag (size includes padding, but we need to align) */
        tag = (multiboot_tag_t *)((uint8_t *)tag + ((tag->size + 7) & ~7));

        /* Safety check - don't read beyond total_size */
        if ((uint64_t)((uint8_t *)tag - (uint8_t *)mbi) >= total_size) {
            break;
        }
    }

    /* Fallback: If no memory info found, use default QEMU memory map */
    if (!found_memory) {
        serial_puts("PMM: No memory info found, using default map for QEMU\n");
        /* QEMU default: 128MB starting at 0 */
        /* Actually, we should use memory above the kernel (which starts at 1MB) */
        /* Add memory from 2MB to 128MB */
        pmm_add_region(0x200000, 128 * 1024 * 1024 - 0x200000);
    }

    serial_puts("PMM: Multiboot2 parsing complete\n");
}
