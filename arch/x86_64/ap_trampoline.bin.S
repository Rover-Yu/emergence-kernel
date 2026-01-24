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
     * Load GDT32 for Protected Mode using position-independent addressing
     * ============================================================
     * The GDTR structure is at offset 0x1E0 within the trampoline.
     * We compute the actual address using call/pop to get current IP,
     * then mask to page boundary and add the GDTR offset.
     * This is necessary because the assembler incorrectly encodes
     * register-indirect LGDT as a hardcoded absolute address.
     * Note: LGDT in 16-bit mode only accepts BX/BP/SI/DI as base regs.
     */
    call get_eip_gdt
get_eip_gdt:
    pop %si               /* SI = current IP */
    /* Round down to 4KB boundary (trampoline base) */
    and $0xF000, %si      /* SI = 0x7000 in real mode */
    /* Add offset to gdt32_gdtr (0x1E0) */
    add $0x1E0, %si       /* SI = 0x71E0 (GDTR address) */
    /* Load GDTR from computed address */
    lgdt (%si)

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

    /* Far jump to protected mode
     * Target address = 0x702D (pm_entry location)
     * Must use absolute linear address for 32-bit offset
     */
    .byte 0x66
    .byte 0xEA
    .long 0x702D
    .word 0x0008

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
     * Load Page Tables using position-independent addressing
     * ============================================================
     * We need to compute the address of boot_pml4_placeholder (at 0x7100)
     * Use call/pop to get current EIP, then adjust to trampoline base
     */
    .byte 0x66           /* 32-bit operand size */
    call get_eip_pm
get_eip_pm:
    pop %eax             /* EAX = address after the call (approx 0x7043) */
    /* Round down to nearest 4KB boundary to get trampoline base (0x7000) */
    .byte 0x66
    and $0xFFFFF000, %eax
    /* Add offset to boot_pml4_placeholder (0x100) to get 0x7100 */
    .byte 0x66
    add $0x100, %eax
    /* Load the 64-bit value at boot_pml4_placeholder */
    .byte 0x67           /* address size prefix for 32-bit address */
    mov (%eax), %eax
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
     * ============================================================
     * Use address 0x7F00 which is properly identity-mapped
     * (within first 1GB, covered by identity-mapped page tables)
     */
    .byte 0x66
    mov $0x7F00, %eax
    lgdt (%eax)

    /* Debug: Output "D" */
    .byte 0x66
    mov $0x3F8, %dx
    mov $'D', %al
    out %al, %dx

    /* ============================================================
     * Far Jump to 64-bit Mode using position-independent addressing
     * ============================================================ */
    .byte 0x66
    call get_eip_lm
get_eip_lm:
    pop %eax             /* EAX = current EIP */
    /* Round down to 0x7000 */
    .byte 0x66
    and $0xFFFFF000, %eax
    /* Add offset to ap_start_placeholder (0x7000 + 0x108 = 0x7108) */
    .byte 0x66
    add $0x108, %eax
    .byte 0x67
    mov (%eax), %eax     /* Load the 64-bit value at ap_start_placeholder */

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
