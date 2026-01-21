/* AP Trampoline - 16/32-bit portion (real mode to protected mode)
 *
 * This file is assembled separately as a 16-bit binary
 * and then included in the main kernel image.
 *
 * The AP starts in real mode at physical address 0x7000.
 */

    .code16
    .org 0

trampoline_start:
    /* ============================================================
     * Step 1: Real Mode - Write debug output
     * ============================================================ */
    mov $0x3F8, %dx
    mov $'A', %al
    out %al, %dx
    mov $'P', %al
    out %al, %dx

    cli

    /* ============================================================
     * Step 2: Set up data segment
     * ============================================================ */
    mov $0x7000, %ax
    mov %ax, %ds

    /* ============================================================
     * Step 3: Load GDT
     * ============================================================ */
    lgdt [gdt32_ptr]

    /* ============================================================
     * Step 4: Enable Protected Mode
     * ============================================================ */
    mov %cr0, %eax
    or $1, %al           /* Set PE bit */
    mov %eax, %cr0

    /* ============================================================
     * Step 5: Far jump to 32-bit protected mode
     * ============================================================ */
    data32 ljmp $0x08, $protected_mode_entry

    .code32
protected_mode_entry:
    /* ============================================================
     * Step 6: Set up data segments
     * ============================================================ */
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss

    /* ============================================================
     * Step 7: Set up temporary stack
     * ============================================================ */
    mov $0x7800, %esp

    /* ============================================================
     * Step 8: Enable PAE
     * ============================================================ */
    mov %cr4, %eax
    or $(1 << 5), %eax
    mov %eax, %cr4

    /* ============================================================
     * Step 9: Load page tables
     * boot_pml4_placeholder will be patched at runtime
     * ============================================================ */
    mov $boot_pml4_placeholder, %ecx
    mov %ecx, %cr3

    /* ============================================================
     * Step 10: Enable Long Mode
     * ============================================================ */
    mov $0xC0000080, %ecx
    rdmsr
    or $(1 << 8), %eax    /* Set LME bit */
    wrmsr

    /* ============================================================
     * Step 11: Enable Paging
     * ============================================================ */
    mov %cr0, %eax
    or $(1 << 31), %eax
    mov %eax, %cr0

    /* ============================================================
     * Step 12: Load 64-bit GDT
     * ============================================================ */
    lgdt [gdt64_ptr]

    /* ============================================================
     * Step 13: Far jump to 64-bit long mode
     * The 64-bit code is in ap_trampoline_64.S
     * ap_start_placeholder will be patched at runtime
     * ============================================================ */
    mov $ap_start_placeholder, %eax
    push $0x18            /* 64-bit code segment selector */
    push %eax             /* Target address */
    ljmp $0x18, $0        /* Far jump (will pop address) */

    /* ============================================================
     * Data Structures - GDTs
     * ============================================================ */

    .align 16
gdt32:
    .long 0, 0                         /* Null descriptor */
    .long 0x00CF9A000000FFFF, 0x00000000  /* 32-bit code segment */
    .long 0x00CF92000000FFFF, 0x00000000  /* 32-bit data segment */
gdt32_end:

gdt32_ptr:
    .word gdt32_end - gdt32 - 1
    .long gdt32

    .align 16
gdt64:
    .long 0, 0                         /* Null descriptor */
    .long 0x00209A0000000000, 0x00000000  /* 64-bit code segment */
    .long 0x0000920000000000, 0x00000000  /* 64-bit data segment */
gdt64_end:

gdt64_ptr:
    .word gdt64_end - gdt64 - 1
    .long gdt64
    .long 0  /* Pad to 8 bytes for .quad equivalent */

    /* ============================================================
     * Placeholder values that will be patched by kernel at runtime
     * ============================================================ */
    .align 4
boot_pml4_placeholder:
    .long 0

ap_start_placeholder:
    .long 0

trampoline_end:
