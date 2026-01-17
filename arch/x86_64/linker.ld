/* JAKernel Linker Script for x86_64 */

ENTRY(_start)

SECTIONS
{
    /* AP Trampoline - must be at physical address 0x7000 (28KB) */
    . = 0x7000;
    .ap_trampoline : {
        *(.ap_trampoline)
    }

    /* Kernel starts at 1MB */
    . = 1M;

    /* Multiboot header must be in first 8KB */
    .multiboot : {
        *(.multiboot)
    }

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
