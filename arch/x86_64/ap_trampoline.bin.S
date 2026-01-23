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
    mov $'H', %al
    out %al, %dx
    mov $'A', %al
    out %al, %dx

    cli

    /* Debug: output '1' after CLI */
    mov $'1', %al
    out %al, %dx

    /* ============================================================
     * Step 2: Set up data segment
     * ============================================================ */
    mov $0x7000, %ax
    mov %ax, %ds

    /* ============================================================
     * Step 3: Load GDT
     * The gdt32_ptr address will be patched at runtime
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
    /* Debug: output '2' before mode switch */
    mov $0x3F8, %dx
    mov $'2', %al
    out %al, %dx

    /* Far jump to protected mode entry */
    ljmp $0x08, $protected_mode_entry

    .code32
protected_mode_entry:
    /* Debug: output '3' in protected mode */
    mov $0x3F8, %edx
    mov $'3', %al
    out %al, %dx
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
    /* Debug: output '4' before paging */
    mov $0x3F8, %edx
    mov $'4', %al
    out %al, %dx

    mov %cr0, %eax
    or $(1 << 31), %eax
    mov %eax, %cr0

    /* Debug: output '5' after paging */
    mov $0x3F8, %edx
    mov $'5', %al
    out %al, %dx

    /* ============================================================
     * Step 12: Load 64-bit GDT
     * ============================================================ */
    lgdt [gdt64_ptr]

    /* Debug: output '6' after GDT64 load */
    mov $0x3F8, %edx
    mov $'6', %al
    out %al, %dx

    /* ============================================================
     * Step 13: Far jump to 64-bit long mode
     * The 64-bit code is in ap_trampoline_64.S
     * ap_start_placeholder will be patched at runtime
     *
     * Use the standard far jump technique: push CS, push addr, retf
     * This is more reliable than lret for mode transitions
     * ============================================================ */
    /* Debug: output '7' before 64-bit jump */
    mov $0x3F8, %edx
    mov $'7', %al
    out %al, %dx

    mov $ap_start_placeholder, %eax
    push $0x18            /* 64-bit code segment selector */
    push %eax             /* Target address */

    /* Jump using retf (far return) which properly loads CS */
    .byte 0xCB            /* RETF opcode */

    /* ============================================================
     * Data Structures - GDTs
     * ============================================================ */

    .align 16
gdt32:
    /* Null descriptor (8 bytes) */
    .long 0, 0
    /* 32-bit code segment (8 bytes) */
    /* Format: [limit15:0][base15:0][base23:16][type][limit19:16+flags][base31:24] */
    /* For base=0, limit=0xFFFF, 32-bit code segment: 0x00CF9A000000FFFF */
    .long 0x0000FFFF, 0x00CF9A00
    /* 32-bit data segment (8 bytes) */
    .long 0x0000FFFF, 0x00CF9200
gdt32_end:

gdt32_ptr:
    .word gdt32_end - gdt32 - 1
    .long gdt32

    .align 16
gdt64:
    /* Null descriptor (8 bytes) */
    .long 0, 0
    /* 64-bit code segment (8 bytes) */
    .long 0x00209A00, 0x00000000
    /* 64-bit data segment (8 bytes) */
    .long 0x00009200, 0x00000000
gdt64_end:

gdt64_ptr:
    .word gdt64_end - gdt64 - 1
    .long gdt64

    /* ============================================================
     * Placeholder values that will be patched by kernel at runtime
     * ============================================================ */
    .align 4
boot_pml4_placeholder:
    .long 0

ap_start_placeholder:
    .long 0

trampoline_end:
