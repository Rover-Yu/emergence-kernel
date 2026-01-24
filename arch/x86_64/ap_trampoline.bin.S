/* AP Trampoline - Real mode to Long Mode (64-bit) transition
 *
 * The AP starts in real mode at physical address 0x7000 via STARTUP IPI.
 * This trampoline transitions: Real Mode -> Protected Mode -> Long Mode (64-bit)
 * Then jumps to ap_start() in the kernel.
 *
 * CRITICAL: This code must be position-independent (works at physical 0x7000)
 *
 * PATCHING: The values 0xAA, 0xBB, 0xCC below are patched at runtime by
 * patch_ap_trampoline() with the actual GDT base addresses.
 */

    .code16
    .org 0

trampoline_start:
    /* Write "HA" to serial */
    cli
    mov $0x3F8, %dx
    mov $'H', %al
    out %al, %dx
    mov $'A', %al
    out %al, %dx

    /* Set up segments */
    mov %cs, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss

    /* Stack */
    mov $0x7C00, %sp

    /* Load GDT - build 6-byte GDTR on stack
     * Format: [limit_low, limit_high, base_0, base_1, base_2, base_3]
     * GDT32 is at 0x70F0, so: limit=0x17, base=0x000070F0
     */
    mov $0x17, %ax
    push %ax
    mov $0x70F0, %ax
    push %ax
    xor %ax, %ax
    push %ax

    mov %sp, %bx
    lgdt (%bx)
    add $6, %sp

    /* Write "1" to serial - GDT loaded */
    mov $0x3F8, %dx
    mov $'1', %al
    out %al, %dx

    /* Write "C" to serial - about to enable protected mode */
    mov $'C', %al
    out %al, %dx

    /* Enable Protected Mode */
    mov %cr0, %eax
    or $1, %al
    mov %eax, %cr0

    /* Write "R" to serial - CR0 updated */
    mov $'R', %al
    out %al, %dx

    /* FAR JUMP to protected mode */
    .byte 0xEA
    .word protected_mode_entry
    .word 0x08

    /* We're now in protected mode (32-bit)
     * All subsequent instructions need operand size prefix 0x66
     * to force 32-bit operation */

protected_mode_entry:
    /* Write "P" to serial - reached protected mode! */
    .byte 0x66            /* Operand size prefix */
    mov $0x3F8, %dx       /* Becomes: mov edx, 0x000003F8 */
    mov $'P', %al
    out %al, %dx

    /* Set up data segments */
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss

    /* Write "2" to serial - in protected mode */
    .byte 0x66
    mov $0x3F8, %dx
    mov $'2', %al
    out %al, %dx

    /* Load boot_pml4 from placeholder - use position-independent addressing */
    call get_eip
get_eip:
    pop %eax
    /* boot_pml4_value offset calculation */
    mov 0xC8(%eax), %eax
    mov %eax, %cr3

    /* Write "3" to serial - CR3 loaded */
    .byte 0x66
    mov $0x3F8, %dx
    mov $'3', %al
    out %al, %dx

    /* Enable PAE */
    mov %cr4, %eax
    or $(1 << 5), %eax
    mov %eax, %cr4

    /* Write "4" to serial - PAE enabled */
    .byte 0x66
    mov $0x3F8, %dx
    mov $'4', %al
    out %al, %dx

    /* Enable Long Mode */
    mov $0xC0000080, %ecx
    rdmsr
    or $(1 << 8), %eax
    wrmsr

    /* Write "5" to serial - long mode enabled */
    .byte 0x66
    mov $0x3F8, %dx
    mov $'5', %al
    out %al, %dx

    /* Enable Paging (this activates Long Mode) */
    mov %cr0, %eax
    or $(1 << 31), %eax
    mov %eax, %cr0

    /* Write "6" to serial - paging enabled */
    .byte 0x66
    mov $0x3F8, %dx
    mov $'6', %al
    out %al, %dx

    /* Load 64-bit GDT */
    mov $0x7BF8, %esp
    mov $0xBB, %eax
    mov %eax, (%esp)
    mov $0xCC00, %eax
    mov %eax, 2(%esp)
    lgdt (%esp)

    /* Write "7" to serial - GDT64 loaded */
    .byte 0x66
    mov $0x3F8, %dx
    mov $'7', %al
    out %al, %dx

    /* Far jump to 64-bit code using lret */
    sub $8, %esp
    call get_eip2
get_eip2:
    pop %eax
    mov 0x5A(%eax), %eax
    mov 0x5E(%eax), %edx
    mov %eax, 2(%esp)
    mov %edx, 6(%esp)
    mov $0x08, %ax
    mov %ax, (%esp)
    lret

    /* GDT32 - Code segment base will be patched to 0x7000 at runtime */
    .align 16
gdt32:
    .quad 0x0000000000000000
    .quad 0x00CF9A000000FFFF
    .quad 0x00CF92000000FFFF

    /* GDT64 */
    .align 16
gdt64:
    .quad 0x0000000000000000
    .quad 0x00AF9A000000FFFF
    .quad 0x00CF92000000FFFF

    /* Patch values at end */
    .align 8
boot_pml4_value:
    .quad 0

ap_start_value:
    .quad 0

trampoline_end:
