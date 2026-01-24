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
     * Target address = 0x703D (pm_entry location in 32-bit mode)
     * Must use absolute linear address for 32-bit offset
     */
    .byte 0x66
    .byte 0xEA
    .long 0x703D
    .word 0x0008

    /* Should never reach here */
    cli
halt_loop:
    hlt
    jmp halt_loop

    /* ============================================================
     * Protected Mode (32-bit) Entry Point
     * ============================================================ */
    .code32
pm_entry:
    /* Debug: Output "P" - reached protected mode */
    mov $0x3F8, %edx
    mov $'P', %al
    out %al, %edx

    /* Set up data segments */
    mov $0x10, %eax
    mov %eax, %ds
    mov %eax, %es
    mov %eax, %ss

    /* ============================================================
     * Load Page Tables using position-independent addressing
     * ============================================================
     * We need to compute the address of boot_pml4_placeholder (at 0x7100)
     * Use call/pop to get current EIP, then adjust to trampoline base
     */
    call get_eip_pm
get_eip_pm:
    pop %eax             /* EAX = address after the call (approx 0x7043) */
    /* Round down to nearest 4KB boundary to get trampoline base (0x7000) */
    and $0xFFFFF000, %eax
    /* Add offset to boot_pml4_placeholder (0x100) to get 0x7100 */
    add $0x100, %eax
    /* Load the 64-bit value at boot_pml4_placeholder */
    mov (%eax), %eax
    mov %eax, %cr3

    /* Debug: Output "3" */
    mov $0x3F8, %edx
    mov $'3', %al
    out %al, %edx

    /* ============================================================
     * Enable PAE (Required for Long Mode)
     * ============================================================ */
    mov %cr4, %eax
    or $(1 << 5), %eax    /* Set PAE bit (bit 5) */
    mov %eax, %cr4

    /* Debug: Output "A" */
    mov $0x3F8, %edx
    mov $'A', %al
    out %al, %edx

    /* ============================================================
     * Enable Long Mode
     * ============================================================ */
    mov $0xC0000080, %ecx
    rdmsr
    or $(1 << 8), %eax    /* Set LME bit (bit 8) */
    wrmsr

    /* Debug: Output "L" */
    mov $0x3F8, %edx
    mov $'L', %al
    out %al, %edx

    /* ============================================================
     * Enable Paging (activates Long Mode)
     * ============================================================ */
    mov %cr0, %eax
    or $(1 << 31), %eax   /* Set PG bit (bit 31) */
    mov %eax, %cr0

    /* Debug: Output "X" */
    mov $0x3F8, %edx
    mov $'X', %al
    out %al, %edx

    /* ============================================================
     * Load 64-bit GDT
     * ============================================================
     * The GDTR structure for GDT64 is at offset 0x1F0 within the trampoline.
     * We need to compute the actual address using position-independent addressing.
     * The GDTR structure contains the base address (0x7F00) and limit of the GDT64.
     */
    call get_eip_gdt64
get_eip_gdt64:
    pop %eax             /* EAX = address after the call */
    /* Round down to 4KB boundary to get trampoline base (0x7000) */
    and $0xFFFFF000, %eax
    /* Add offset to gdt64_gdtr (0x1F0) to get 0x71F0 */
    add $0x1F0, %eax
    /* Load GDTR from computed address */
    lgdt (%eax)

    /* Debug: Output "D" */
    mov $0x3F8, %edx
    mov $'D', %al
    out %al, %edx

    /* ============================================================
     * Far Jump to 64-bit Mode using retf technique
     * ============================================================
     * Instead of using ljmp (which doesn't work properly for this
     * mode transition), we use retf (far return). We push the target
     * selector and offset onto the stack, then execute retf.
     */
    /* Push 64-bit code segment selector */
    pushl $0x0008            /* Selector for 64-bit code segment */
    /* Push target offset (where stub is located) */
    pushl $0x8000            /* Offset to ap_trampoline_stub */
    /* Execute far return - pops CS:RIP and switches to 64-bit mode */
    retf

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

    /* ============================================================
     * AP Trampoline Stub - 64-bit entry point at offset 0x1000
     * ============================================================
     * This stub is placed at offset 0x1000 (physical 0x8000 when loaded)
     * and handles the final 64-bit jump to ap_start.
     * The 32-bit trampoline jumps here after enabling Long Mode.
     */
    .org 0x1000
    .code64
ap_trampoline_stub:
    /* Debug: Output 'S' to serial (stub reached in 64-bit mode) */
    mov $0x3F8, %edx
    mov $'S', %al
    out %al, %edx

    /* Set up AP stack */
    /* The stack address is patched at physical 0x8100 (0x7000 + 0x1100)
     * Since stub runs at 0x8000 and trampoline is at 0x7000,
     * we use absolute address 0x8100 directly. */
    movq $0x8100, %rax        /* Absolute address of ap_stack_placeholder */

    /* Debug: Output first byte of stack address to verify mapping */
    mov (%rax), %r8
    test %r8, %r8
    jz stack_zero

    /* Debug: Output 'Q' to confirm we can read 0x8100 */
    mov $'Q', %al
    out %al, %edx

    mov (%rax), %rsp          /* Load stack pointer */

    /* Debug: Output 'W' to confirm RSP was loaded */
    mov $'W', %al
    out %al, %edx

    /* Debug: Output first byte of RSP to verify stack address */
    mov %rsp, %r8
    and $0xFF, %r8d
    test %r8, %r8
    jz rsp_zero
    /* RSP is non-zero */
    mov $'R', %al
    out %al, %edx
    jmp stack_ok
rsp_zero:
    /* RSP is zero - output 'Y' */
    mov $'Y', %al
    out %al, %edx
stack_ok:
    jmp stack_continue
stack_zero:
    /* Stack address is zero - output 'Z' */
    mov $'Z', %al
    out %al, %edx
stack_continue:

    /* Clear segment registers */
    mov $0, %ax
    mov %ax, %ss
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    /* Debug: Output 'A' to serial (after segment registers cleared) */
    mov $'A', %al
    out %al, %edx

    /* Skip stack test - just try to jump to ap_start */
    /* Debug: Output 'T' to serial (about to jump to ap_start) */
    mov $'T', %al
    out %al, %edx

    /* Load ap_start address from placeholder */
    /* ap_start_placeholder is at physical 0x7108 (0x7000 + 0x108) */
    movq $0x7108, %rax        /* Absolute address of ap_start_placeholder */
    mov (%rax), %rax          /* Load ap_start address */

    /* Debug: Output '1' to confirm we loaded ap_start address */
    mov $'1', %al
    out %al, %edx

    /* ============================================================
     * Jump to ap_start using jmp instead of call
     * ============================================================
     * Using jmp avoids pushing to stack, which eliminates the
     * stack dependency. The ap_start() function doesn't return
     * anyway (it halts at the end), so jmp is appropriate here.
     *
     * If this works, we know the call instruction was the issue.
     * If this fails, the problem is with ap_start itself.
     */
    cli                     /* Disable interrupts before jump */
    jmp *%rax               /* Jump to ap_start (no return) */

    /* Should never reach here */

    /* Should never reach here */
ap_stub_halt:
    cli
    hlt
    jmp ap_stub_halt

    /* Placeholder for AP stack address (patched by boot.S) */
    .org 0x1100
ap_stack_placeholder:
    .quad 0xDEADBEEFCAFEBABE  /* Patched with actual ap_stack_end address */

trampoline_end:
