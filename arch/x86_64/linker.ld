/* JAKernel Linker Script for x86_64 */

ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(7);      /* R+W+E for kernel code */
    rodata PT_LOAD FLAGS(5);    /* R+E for read-only data */
    data PT_LOAD FLAGS(6);      /* R+W for data/bss */
    trampoline PT_LOAD FLAGS(7); /* R+W+E for trampoline code */
}

SECTIONS
{
    /* ============================================================
     * Kernel code starts at 1MB
     * ============================================================
     * The multiboot header MUST be within the first 8192 bytes of the kernel.
     * We start with the kernel at 1MB, then place the trampoline separately.
     */
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

    /* ============================================================
     * AP Trampoline at fixed physical address 0x7000
     * ============================================================
     * The trampoline must be at a fixed physical address so the BSP
     * can tell APs where to start via STARTUP IPI.
     *
     * We place these sections AFTER the main kernel sections in the file,
     * but load them at physical address 0x7000 using AT().
     *
     * This ensures:
     * 1. The multiboot header is at the start of the first LOAD segment
     * 2. The trampoline is loaded at the correct physical address
     */
    /* Real mode and protected mode code */
    .ap_trampoline 0x7000 : AT(0x7000) {
        *(.ap_trampoline)
    } :trampoline

    /* GDT64 at offset 0x0F00 (physical 0x7F00) */
    .ap_trampoline_gdt64 0x7F00 : AT(0x7F00) {
        *(.ap_trampoline_gdt64)
    } :trampoline

    /* 64-bit stub at 0x8000 */
    .ap_trampoline_stub 0x8000 : AT(0x8000) {
        *(.ap_trampoline_stub)
    } :trampoline

    /* GOT - at fixed offset 0x2000 from trampoline base (0x7000 + 0x2000 = 0x9000) */
    .ap_trampoline_got 0x9000 : AT(0x9000) {
        *(.ap_trampoline_got)
    } :trampoline
}
