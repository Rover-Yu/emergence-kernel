/* AP Trampoline - Real mode to Long Mode (64-bit) transition
 *
 * The AP starts in real mode at physical address 0x7000 via STARTUP IPI.
 * This trampoline transitions: Real Mode -> Protected Mode -> Long Mode (64-bit)
 * Then jumps to ap_start() in the kernel.
 *
 * CRITICAL: This code must be position-independent (works at physical 0x7000)
 *
 * PATCHING: The values below are patched at runtime by patch_ap_trampoline():
 * - GDTR base bytes at 0x7BE0 (GDT32) and 0x7BF0 (GDT64)
 * - GDT descriptor base addresses for position independence
 * - boot_pml4_value: Address of PML4 page table
 * - ap_start_value: Address of ap_start() function
 */

    .code16
    .org 0

trampoline_start:
    cli

    /* Debug output "HA" to serial (COM1 at 0x3F8) */
    mov $0x3F8, %dx
    mov $'H', %al
    out %al, %dx
    mov $'A', %al
    out %al, %dx

    /* ============================================================
     * Real Mode Setup
     * ============================================================ */
    mov %cs, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov $0x7C00, %sp

    /* Debug: Output "1" */
    mov $0x3F8, %dx
    mov $'1', %al
    out %al, %dx

    /* ============================================================
     * Load GDT32 for Protected Mode
     * ============================================================
     * Build GDTR at fixed location 0x7BE0:
     * - Limit = 0x17 (23 bytes for 3 descriptors)
     * - Base = patched at runtime (bytes 2-5)
     */
    mov $0x7BE0, %bx
    movb $0x20, (%bx)      /* Limit = 32 (extra room, ensures we don't have limit issues) */
    movb $0x00, 1(%bx)
    /* Bytes 2-5 are patched by patch_ap_trampoline() */
    lgdt (%bx)

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

    /* Short jump to serialize */
    jmp 1f
1:
    /* Debug: Output "E" */
    mov $0x3F8, %dx
    mov $'E', %al
    out %al, %dx

    /* Ensure stack is valid before far jump */
    mov $0x7C00, %sp

    /* Far jump to protected mode
     * Use 32-bit far jump like Linux does:
     * 0x66 = operand size prefix (for 32-bit offset)
     * 0xEA = far jump opcode
     */
    .byte 0x66
    .byte 0xEA
    .long pm_entry_offset    /* 32-bit offset */
    .word 0x08               /* Selector */

    /* Should never reach here */
    cli
halt_loop:
    hlt
    jmp halt_loop

    /* ============================================================
     * Protected Mode (32-bit) Entry Point
     * ============================================================
     * NOTE: Keep everything in .code16 and use 0x66 prefix for 32-bit operands
     * This prevents the assembler from inserting padding at mode boundaries
     */
    .code16
pm_entry:
    /* First, test if we actually got here - output a unique pattern */
    .byte 0x66           /* operand size prefix for 32-bit */
    mov $0x3F8, %dx
    mov $'!', %al
    out %al, %dx
    mov $'!', %al
    out %al, %dx
    mov $'!', %al
    out %al, %dx

    /* Then halt */
    hlt

    /* Debug: Output "R" */
    .byte 0x66
    mov $0x3F8, %dx
    mov $'R', %al
    out %al, %dx

    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss

    /* Debug: Output "S" */
    .byte 0x66
    mov $0x3F8, %dx
    mov $'S', %al
    out %al, %dx

    /* ============================================================
     * Load Page Tables
     * ============================================================ */
    .byte 0x66
    mov $0x7000, %eax
    add $boot_pml4_offset, %eax
    .byte 0x67           /* address size prefix for 32-bit address */
    mov (%eax), %eax
    mov %eax, %cr3

    /* Debug: Output "3" */
    .byte 0x66
    mov $0x3F8, %dx
    mov $'3', %al
    out %al, %dx

    /* ============================================================
     * Enable Long Mode
     * ============================================================ */
    .byte 0x66
    mov $0xC0000080, %ecx
    rdmsr
    or $(1 << 8), %eax
    wrmsr

    /* Debug: Output "L" */
    .byte 0x66
    mov $0x3F8, %dx
    mov $'L', %al
    out %al, %dx

    /* ============================================================
     * Enable Paging
     * ============================================================ */
    .byte 0x66
    mov %cr0, %eax
    or $(1 << 31), %eax
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
    mov $0x7BF0, %ebx
    .byte 0x66
    mov $0x17, %eax
    mov %eax, (%ebx)
    lgdt (%ebx)

    /* Debug: Output "D" */
    .byte 0x66
    mov $0x3F8, %dx
    mov $'D', %al
    out %al, %dx

    /* ============================================================
     * Far Jump to 64-bit Mode
     * ============================================================ */
    .byte 0x66
    mov $0x7000, %eax
    add $ap_start_offset, %eax
    .byte 0x67
    mov (%eax), %eax

    pushw $0x08
    push %eax

    /* Debug: Output "J" */
    .byte 0x66
    mov $0x3F8, %dx
    mov $'J', %al
    out %al, %dx

    lret

    .code64
halt:
    hlt
    jmp halt

    /* ============================================================
     * GDT32 - For 32-bit Protected Mode
     * ============================================================ */
    .align 16
gdt32:
    .quad 0x0000000000000000
    .quad 0x00CF9A000000FFFF
    .quad 0x00CF92000000FFFF

    /* ============================================================
     * GDT64 - For 64-bit Long Mode
     * ============================================================ */
    .align 16
gdt64:
    .quad 0x0000000000000000
    .quad 0x00209A0000000000
    .quad 0x0000920000000000

    /* ============================================================
     * Patchable Values
     * ============================================================ */
    .align 8
boot_pml4_value:
    .quad 0

ap_start_value:
    .quad 0

trampoline_end:

/* These are bytes that will be patched at runtime */
gdt32_base_byte:
    .byte 0

gdt64_base_byte:
    .byte 0

/* Assembly-time constants */
.set boot_pml4_offset, boot_pml4_value - trampoline_start
.set ap_start_offset, ap_start_value - trampoline_start
.set gdt32_offset, gdt32 - trampoline_start
.set gdt64_offset, gdt64 - trampoline_start
.set pm_entry_offset, pm_entry - trampoline_start
