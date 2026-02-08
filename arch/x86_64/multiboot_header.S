/* Multiboot2 Header - Standalone Header File
 *
 * This file contains the multiboot2 header for the Emergence Kernel.
 * It is assembled as a 64-bit object and linked into the .multiboot section.
 *
 * The header is placed at the beginning of the kernel's .text section
 * (via the linker script) so GRUB can find it when parsing the ELF file.
 *
 * GRUB 2.06 can find multiboot headers anywhere in the ELF file by
 * scanning the program headers, not just the first 8KB.
 */

.section .multiboot, "a"
.align 8
.global multiboot_header_start
multiboot_header_start:
    /* magic */
    .long 0xe85250d6
    /* architecture: i386 */
    .long 0
    /* header length */
    .long multiboot_header_end - multiboot_header_start
    /* checksum */
    .long -(0xe85250d6 + 0 + (multiboot_header_end - multiboot_header_start))

    /* Information request tag - request cmdline and mmap */
    .short 1                    /* type: information request */
    .short 0                    /* flags */
    .long 16                    /* size */

    /* Requested tags */
    .long 1                     /* MULTIBOOT_TAG_CMDLINE */
    .long 6                     /* MULTIBOOT_TAG_MMAP */

    /* End tag */
    .short 0                    /* type: end tag */
    .short 0                    /* flags */
    .long 8                     /* size */
multiboot_header_end:
