/* AP Trampoline - Real mode to Long Mode (64-bit) transition
 *
 * The AP starts in real mode at physical address 0x7000 via STARTUP IPI.
 * This trampoline transitions: Real Mode -> Protected Mode -> Long Mode (64-bit)
 * Then jumps to ap_start() in the kernel.
 *
 * CRITICAL: This code must be position-independent (works at physical 0x7000)
 *
 * RUNTIME PATCHING (done in boot.S during copy):
 * - boot_pml4_placeholder (8 bytes at offset 0x100): PML4 page table address
 * - ap_start_placeholder (8 bytes at offset 0x108): ap_start function address
 */

    .code16
    .org 0

trampoline_start:
    cli

    /* ============================================================
     * Real Mode Setup
     * ============================================================ */
    mov %cs, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov $0x7C00, %sp

    /* Debug output "H" to serial (COM1 at 0x3F8) */
    mov $0x3F8, %dx
    mov $'H', %al
    out %al, %dx

    /* ============================================================
     * Load GDT32 for Protected Mode
     * ============================================================ */
    lgdt 0x7BE0    /* GDTR at fixed address with pre-filled values */

    /* Debug: Output "G" */
    mov $0x3F8, %dx
    mov $'G', %al
    out %al, %dx

    /* ============================================================
     * Enable Protected Mode
     * ============================================================ */
    mov %cr0, %eax
    or $1, %eax    /* Set PE (Protected Mode Enable) */
    mov %eax, %cr0

    /* Far jump to protected mode (flushes instruction queue) */
    .byte 0x66        /* 32-bit operand size prefix */
    .byte 0xEA        /* far jump opcode */
    .word pm_entry    /* offset */
    .word 0x08        /* selector */

    /* Should never reach here */
    cli
halt_loop:
    hlt
    jmp halt_loop

    /* ============================================================
     * Protected Mode (32-bit) Entry Point
     * ============================================================ */
    .code16
pm_entry:
    /* Debug: Output "P" - reached protected mode */
    .byte 0x66
    mov $0x3F8, %dx
    mov $'P', %al
    out %al, %dx

    /* Set up data segments */
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss

    /* ============================================================
     * Load Page Tables
     * boot_pml4_placeholder is patched at runtime
     * ============================================================ */
    .byte 0x66
    mov $boot_pml4_placeholder, %eax
    mov %eax, %cr3

    /* Debug: Output "3" */
    .byte 0x66
    mov $0x3F8, %dx
    mov $'3', %al
    out %al, %dx

    /* ============================================================
     * Enable PAE (Required for Long Mode)
     * ============================================================ */
    .byte 0x66
    mov %cr4, %eax
    or $(1 << 5), %eax    /* Set PAE bit (bit 5) */
    mov %eax, %cr4

    /* Debug: Output "A" */
    .byte 0x66
    mov $0x3F8, %dx
    mov $'A', %al
    out %al, %dx

    /* ============================================================
     * Enable Long Mode
     * ============================================================ */
    .byte 0x66
    mov $0xC0000080, %ecx
    rdmsr
    or $(1 << 8), %eax    /* Set LME bit (bit 8) */
    wrmsr

    /* Debug: Output "L" */
    .byte 0x66
    mov $0x3F8, %dx
    mov $'L', %al
    out %al, %dx

    /* ============================================================
     * Enable Paging (activates Long Mode)
     * ============================================================ */
    .byte 0x66
    mov %cr0, %eax
    or $(1 << 31), %eax   /* Set PG bit (bit 31) */
    mov %eax, %cr0

    /* Debug: Output "X" */
    .byte 0x66
    mov $0x3F8, %dx
    mov $'X', %al
    out %al, %dx

    /* ============================================================
     * Load 64-bit GDT
     * ============================================================ */
    .byte 0x66
    mov $0x7BF0, %eax
    lgdt (%eax)

    /* Debug: Output "D" */
    .byte 0x66
    mov $0x3F8, %dx
    mov $'D', %al
    out %al, %dx

    /* ============================================================
     * Far Jump to 64-bit Mode
     * ap_start_placeholder is patched at runtime
     * ============================================================ */
    .byte 0x66
    mov $ap_start_placeholder, %eax

    pushw $0x08
    push %eax
    lret

    .code64
halt:
    hlt
    jmp halt

    /* ============================================================
     * Placeholder Values (patched by boot.S during copy)
     * ============================================================ */
    .org 0x100
boot_pml4_placeholder:
    .quad 0xDEADBEEFCAFEBABE  /* Patched with actual boot_pml4 address */

    .org 0x108
ap_start_placeholder:
    .quad 0xDEADBEEFCAFEBABE  /* Patched with actual ap_start address */

    /* ============================================================
     * GDTR Structures (at fixed addresses)
     * ============================================================ */
    .org 0x1E0
gdt32_gdtr:
    .word 0x17        /* Limit = 23 bytes (3 descriptors) */
    .long 0x00007E00  /* Base = 0x7E00 */

    .org 0x1F0
gdt64_gdtr:
    .word 0x17        /* Limit = 23 bytes (3 descriptors) */
    .long 0x00007F00  /* Base = 0x7F00 */

    /* ============================================================
     * GDT32 - For 32-bit Protected Mode
     * ============================================================ */
    .org 0xE00
gdt32:
    .quad 0x0000000000000000                       /* Null descriptor */
    .quad 0x00CF9A000000FFFF                      /* 32-bit code segment */
    .quad 0x00CF92000000FFFF                      /* 32-bit data segment */

    /* ============================================================
     * GDT64 - For 64-bit Long Mode
     * ============================================================ */
    .org 0xF00
gdt64:
    .quad 0x0000000000000000                       /* Null descriptor */
    .quad 0x00209A0000000000                       /* 64-bit code segment */
    .quad 0x0000920000000000                       /* 64-bit data segment */

trampoline_end:
