/* Emergence Kernel - Multiboot2 parser */

#include <stdint.h>
#include <stddef.h>
#include "arch/x86_64/multiboot2.h"
#include "kernel/pmm.h"
#include "arch/x86_64/serial.h"

/* External function for serial output */
extern void serial_puts(const char *str);
extern void serial_putc(char c);

/* Global kernel command line (max 1024 bytes) */
static char kernel_cmdline[1024];

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

/* Parse multiboot2 command line tag */
static void parse_cmdline(multiboot_tag_cmdline_t *tag) {
    const char *src = tag->cmdline;
    size_t i = 0;

    /* Copy cmdline string to global buffer, truncate to 1023 bytes */
    while (i < sizeof(kernel_cmdline) - 1 && src[i] != '\0') {
        kernel_cmdline[i] = src[i];
        i++;
    }

    /* Ensure null-termination */
    kernel_cmdline[i] = '\0';
    serial_puts("CMDLINE: Parsed from multiboot: ");
    serial_puts(kernel_cmdline);
    serial_puts("\n");
}

/* Simple key-value parser for cmdline */
/* Format: key1=value1 key2=value2 or key1="quoted value" */
const char *cmdline_get_value(const char *key) {
    static char value_buf[256];
    const char *p = kernel_cmdline;
    size_t key_len = 0;

    /* Get key length */
    const char *key_end = key;
    while (*key_end != '\0') {
        key_len++;
        key_end++;
    }

    /* Search for key=value pattern */
    while (*p != '\0') {
        /* Skip leading whitespace */
        while (*p == ' ') p++;

        /* Check if this token starts with our key */
        if (p[0] == '-' && p[1] == '-') {
            p += 2; /* Skip '--' */
        }

        /* Compare key */
        size_t i = 0;
        int match = 1;
        while (i < key_len && p[i] != '\0' && p[i] != '=' && p[i] != ' ') {
            if (p[i] != key[i]) {
                match = 0;
                break;
            }
            i++;
        }

        /* Check for match followed by '=' */
        if (match && p[i] == '=') {
            /* Found it! Copy value */
            p += i + 1; /* Skip key and '=' */

            /* Check if value is quoted */
            int in_quotes = 0;
            if (*p == '"') {
                in_quotes = 1;
                p++; /* Skip opening quote */
            }

            size_t v = 0;
            while (*p != '\0' && v < sizeof(value_buf) - 1) {
                if (in_quotes) {
                    if (*p == '"') {
                        p++; /* Skip closing quote */
                        break;
                    }
                    value_buf[v++] = *p++;
                } else {
                    if (*p == ' ') break;
                    value_buf[v++] = *p++;
                }
            }
            value_buf[v] = '\0';
            return value_buf;
        }

        /* Skip to next token */
        while (*p != '\0' && *p != ' ') p++;
    }

    return NULL;
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

    /* Check if multiboot info is valid */
    if (mbi_addr == 0) {
        serial_puts("PMM: No multiboot info provided\n");
        goto use_default_cmdline;
    }

    total_size = mbi->total_size;

    /* If total_size is 0 or too small, boot loader didn't provide info */
    if (total_size < sizeof(multiboot_info_t)) {
        serial_puts("PMM: Invalid multiboot info (size=");
        put_hex(total_size);
        serial_puts("), using defaults\n");
        goto use_default_cmdline;
    }

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
            case MULTIBOOT_TAG_CMDLINE:
                serial_puts("CMDLINE: Found cmdline tag in multiboot info\n");
                parse_cmdline((multiboot_tag_cmdline_t *)tag);
                break;

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

use_default_cmdline:
    /* Set default cmdline if multiboot info was invalid */
    if (kernel_cmdline[0] == '\0') {
        /*
         * NOTE: In QEMU with GRUB, the multiboot info structure may not pass
         * the command line correctly. For testing purposes, you can modify the
         * default_cmdline below to specify which tests to run.
         *
         * Examples:
         *   ""              - No tests run (default for production)
         *   "test=all"      - Run all auto_run tests
         *   "test=slab"     - Run only slab test
         *   "test=unified"  - Run all tests in unified mode
         *
         * On real hardware with a proper bootloader, use KERNEL_CMDLINE in Makefile
         * or pass via GRUB configuration.
         */
        const char *default_cmdline = "";  /* Default: no tests */
        size_t i = 0;
        while (i < sizeof(kernel_cmdline) - 1 && default_cmdline[i] != '\0') {
            kernel_cmdline[i] = default_cmdline[i];
            i++;
        }
        kernel_cmdline[i] = '\0';
        if (default_cmdline[0] != '\0') {
            serial_puts("CMDLINE: Using default: ");
            serial_puts(kernel_cmdline);
            serial_puts("\n");
        } else {
            serial_puts("CMDLINE: Using default (empty - no tests will run)\n");
        }
    }

fallback:
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

/* Accessor function to get kernel command line */
const char *multiboot_get_cmdline(void) {
    serial_puts("CMDLINE: Parsing kernel command line...\n");

    if (kernel_cmdline[0] != '\0') {
        serial_puts("CMDLINE: Raw: '");
        serial_puts(kernel_cmdline);
        serial_puts("'\n");

        /* Parse and display known keys */
        const char *quote = cmdline_get_value("quote");
        if (quote != NULL) {
            serial_puts("CMDLINE: quote=\"");
            serial_puts(quote);
            serial_puts("\"\n");
        }

        const char *author = cmdline_get_value("author");
        if (author != NULL) {
            serial_puts("CMDLINE: author=");
            serial_puts(author);
            serial_puts("\n");
        }

        serial_puts("CMDLINE: Parsing complete\n");
    } else {
        serial_puts("CMDLINE: No command line specified\n");
    }

    return kernel_cmdline;
}
