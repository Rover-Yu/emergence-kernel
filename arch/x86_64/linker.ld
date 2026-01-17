/* JAKernel Linker Script for x86_64 */

ENTRY(_start)

SECTIONS
{
    /* Multiboot header MUST be at the beginning of the file
     * GRUB reads the ELF file and looks for the multiboot header
     * within the first 8KB (or 32KB for multiboot2) of the file
     * This section is placed at address 0 to ensure it's at file offset 0 */
    . = 0;
    .multiboot : {
        *(.multiboot)
    }

    /* AP Trampoline - must be at physical address 0x7000 (28KB) */
    . = 0x7000;
    .ap_trampoline : {
        *(.ap_trampoline)
    }

    /* Kernel starts at 1MB */
    . = 1M;

    /* Text section (code) - both 32-bit and 64-bit */
    .text ALIGN(4K) : {
        *(.text*)
    }

    /* Read-only data (GDT goes here) */
    .rodata ALIGN(4K) : {
        *(.rodata*)
    }

    /* Initialized data */
    .data ALIGN(4K) : {
        *(.data*)
    }

    /* Uninitialized data (stack and page tables) */
    .bss ALIGN(4K) : {
        *(COMMON)
        *(.bss*)
    }

    /* End of kernel */
    . = ALIGN(4K);
    _kernel_end = .;
}
