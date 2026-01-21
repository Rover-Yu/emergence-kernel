/* JAKernel Linker Script for x86_64 */

ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(7);      /* R+W+E for kernel code */
    rodata PT_LOAD FLAGS(5);    /* R+E for read-only data */
    data PT_LOAD FLAGS(6);      /* R+W for data/bss */
}

SECTIONS
{
    /* ============================================================
     * Kernel code starts at 1MB
     * ============================================================ */
    . = 1M;

    /* Text section - multiboot header first, then code */
    .text ALIGN(4K) : {
        *(.multiboot)       /* Must be first for GRUB */
        *(.text*)
    } :text

    .rodata ALIGN(4K) : {
        *(.rodata*)
    } :rodata

    .data ALIGN(4K) : {
        *(.data*)
    } :data

    .bss ALIGN(4K) : {
        *(COMMON)
        *(.bss*)
    } :data

    . = ALIGN(4K);
    _kernel_end = .;
}
