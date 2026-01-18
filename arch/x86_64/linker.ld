/* JAKernel Linker Script for x86_64 */

ENTRY(_start)

PHDRS
{
    lowmem PT_LOAD FLAGS(7);    /* R+W+E for multiboot and trampoline */
    text PT_LOAD FLAGS(7);      /* R+W+E for kernel code */
    rodata PT_LOAD FLAGS(5);    /* R+E for read-only data */
    data PT_LOAD FLAGS(6);      /* R+W for data/bss */
}

SECTIONS
{
    /* ============================================================
     * Low Memory Region (0 - 32KB)
     * Contains Multiboot header and AP trampoline
     * ============================================================ */
    . = 0;

    /* Multiboot header at 0 */
    .multiboot : {
        *(.multiboot)
    } :lowmem

    /* AP trampoline at 0x7000 - MUST be at this exact address for STARTUP IPI */
    . = 0x7000;
    .ap_trampoline : {
        *(.ap_trampoline)
    } :lowmem

    /* ============================================================
     * Kernel proper (1MB and above)
     * ============================================================ */
    . = 1M;

    .text ALIGN(4K) : {
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
