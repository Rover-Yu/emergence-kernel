/* AP Trampoline - Real mode to Long Mode (64-bit) transition
 *
 * The AP starts in real mode at physical address 0x7000 via STARTUP IPI.
 * This trampoline transitions: Real Mode -> Protected Mode -> Long Mode (64-bit)
 * Then jumps to ap_start() in the kernel.
 *
 * CRITICAL: This code uses PIC (Position-Independent Code) with GOT-based addressing.
 * The linker populates the GOT at build time - NO runtime patching needed!
 *
 * TRAMPOLINE MEMORY LAYOUT (physical addresses):
 *   0x7000 - Real mode entry point (.ap_trampoline)
 *   0x7F00 - GDT64 (.ap_trampoline_gdt64)
 *   0x8000 - 64-bit stub (.ap_trampoline_stub)
 *   GOT entries are accessed via RIP-relative addressing in 64-bit mode
 *   or call/pop technique in 16/32-bit modes
 */

/* ========================================================================
 * CONSTANTS - Hardware addresses and offsets
 * ======================================================================== */

/* Serial port (COM1) */
.equ SERIAL_PORT,        0x03F8  /* COM1 base I/O address */

/* Trampoline memory layout */
.equ TRAMPOLINE_BASE,    0x7000  /* Physical address where trampoline is loaded */
.equ REAL_MODE_STACK,    0x7C00  /* Stack pointer for real mode (just below trampoline) */

/* Offsets within trampoline */
.equ OFFSET_GDT32_GDTR,  0x01E0  /* GDTR for 32-bit protected mode GDT */
.equ OFFSET_GDT64_GDTR,  0x01F0  /* GDTR for 64-bit long mode GDT */

/* GDT base addresses (computed offsets from trampoline base) */
.equ GDT32_BASE_OFFSET,  0x0E00  /* 32-bit GDT offset from trampoline start */
.equ GDT64_BASE_OFFSET,  0x0F00  /* 64-bit GDT offset from trampoline start */

/* 64-bit stub physical address */
.equ STUB_PHYSICAL,      0x8000  /* Stub physical address (0x7000 + 0x1000) */

/* Control register bits */
.equ CR4_PAE_BIT,        0x20    /* PAE enable bit in CR4 */
.equ CR4_PSE_BIT,        0x10    /* PSE enable bit in CR4 */
.equ EFER_LME_BIT,       0x1000  /* Long Mode Enable bit in IA32_EFER */
.equ EFER_NXE_BIT,       0x800   /* No-Execute Enable bit in IA32_EFER */
.equ CR0_PG_BIT,         0x80000000 /* Paging enable bit in CR0 */

/* MSRs */
.equ MSR_EFER,           0xC0000080  /* Extended Feature Enable Register MSR */

/* Code selectors */
.selector = 0x0008

/* External symbols - populated into GOT by linker */
.extern boot_pml4        /* PML4 page table address */
.extern ap_start         /* AP entry point function */
.extern idt_ptr          /* IDT pointer structure in kernel BSS */
.extern ap_stack_end     /* End of AP stack (top address) */

/* ============================================================
 * Real mode and protected mode code section
 * ============================================================ */
.section .ap_trampoline, "ax"
.global trampoline_start
trampoline_start:
    .code16

    cli

    /* ========================================================================
     * Real Mode Setup
     * ======================================================================== */
    mov %cs, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov $REAL_MODE_STACK, %sp

    /* Debug output "H" to serial (COM1) */
    mov $SERIAL_PORT, %dx
    mov $'H', %al
    out %al, %dx

    /* ========================================================================
     * Load GDT32 for Protected Mode using position-independent addressing
     * ======================================================================== */
    call get_eip_gdt
get_eip_gdt:
    pop %si               /* SI = current IP */
    /* Round down to 4KB boundary (trampoline base) */
    and $TRAMPOLINE_BASE, %si  /* SI = 0x7000 in real mode */
    /* Add offset to gdt32_gdtr */
    add $OFFSET_GDT32_GDTR, %si  /* SI = 0x71E0 (GDTR address) */
    /* Load GDTR from computed address */
    lgdt (%si)

    /* Debug: Output "G" */
    mov $SERIAL_PORT, %dx
    mov $'G', %al
    out %al, %dx

    /* ============================================================
     * Enable Protected Mode
     * ============================================================ */
    mov %cr0, %eax
    or $1, %eax    /* Set PE (Protected Mode Enable) */
    mov %eax, %cr0

    /* Far jump to protected mode */
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
    /* Debug: Output "3" - reached protected mode */
    mov $SERIAL_PORT, %edx
    mov $'3', %al
    out %al, %edx

    /* Set up data segments */
    mov $0x10, %eax
    mov %eax, %ds
    mov %eax, %es
    mov %eax, %ss

    /* ============================================================
     * Enable PAE (Required for Long Mode) - DO THIS FIRST!
     * ============================================================ */
    mov %cr4, %eax
    or $(1 << 5), %eax    /* Set PAE bit (bit 5) */
    mov %eax, %cr4

    /* Debug: Output "A" */
    mov $0x3F8, %edx
    mov $'A', %al
    out %al, %edx

    /* ============================================================
     * Load CR3 using GOT-based PIC (32-bit mode)
     * ============================================================
     * In 32-bit mode, we use call/pop to get EIP, then compute GOT address.
     * GOT is at fixed offset 0x2000 from trampoline base.
     */
    call get_eip_pm
get_eip_pm:
    pop %ebx              /* EBX = current EIP */
    and $TRAMPOLINE_BASE, %ebx  /* Round to 4KB boundary (0x7000) */
    add $0x2000, %ebx     /* EBX = GOT address (0x7000 + 0x2000 = 0x9000) */
    mov (%ebx), %eax      /* Load boot_pml4 from GOT[0] */
    mov %eax, %cr3

    /* CRITICAL: TLB flush after loading CR3 */
    mov %cr3, %eax
    mov %eax, %cr3

    /* Debug: Output "P" */
    mov $0x3F8, %edx
    mov $'P', %al
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
     * ============================================================ */
    call get_eip_gdt64
get_eip_gdt64:
    pop %eax              /* EAX = address after the call */
    and $TRAMPOLINE_BASE, %eax  /* Round down to 4KB boundary */
    add $OFFSET_GDT64_GDTR, %eax  /* Add GDT64 GDTR offset */
    lgdt (%eax)

    /* Debug: Output "D" */
    mov $0x3F8, %edx
    mov $'D', %al
    out %al, %edx

    /* ============================================================
     * Far Jump to 64-bit Mode using retf technique
     * ============================================================ */
    pushl $0x0008         /* 64-bit code segment selector */
    pushl $STUB_PHYSICAL  /* Offset to ap_trampoline_stub (0x8000) */
    retf

    .code64
halt:
    hlt
    jmp halt

    /* ============================================================
     * GDTR Structures (at fixed addresses within .ap_trampoline)
     * ============================================================ */
    .org 0x1E0
gdt32_gdtr:
    .word 0x17        /* Limit = 23 bytes (3 descriptors) */
    .long TRAMPOLINE_BASE + GDT32_BASE_OFFSET  /* Base = 0x7000 + 0x0E00 = 0x7E00 */

    .org 0x1F0
gdt64_gdtr:
    .word 0x17        /* Limit = 23 bytes (3 descriptors) */
    .long TRAMPOLINE_BASE + GDT64_BASE_OFFSET  /* Base = 0x7000 + 0x0F00 = 0x7F00 */

    /* ============================================================
     * GDT32 - For 32-bit Protected Mode
     * ============================================================ */
    .org 0xE00
gdt32:
    .quad 0x0000000000000000                       /* Null descriptor */
    .quad 0x00CF9A000000FFFF                      /* 32-bit code segment */
    .quad 0x00CF92000000FFFF                      /* 32-bit data segment */

    .size trampoline_start, . - trampoline_start

/* ============================================================
 * GDT64 - For 64-bit Long Mode at physical 0x7F00
 * ============================================================ */
.section .ap_trampoline_gdt64, "a"
gdt64:
    .quad 0x0000000000000000                       /* Null descriptor */
    .quad 0x00209A0000000000                       /* 64-bit code segment */
    .quad 0x0000920000000000                       /* 64-bit data segment */

/* ============================================================
 * AP Trampoline Stub - 64-bit entry point at physical 0x8000
 * ============================================================ */
.section .ap_trampoline_stub, "ax"
.code64
.global ap_trampoline_stub
ap_trampoline_stub:
    /* Debug: Output 'S' to serial (stub reached in 64-bit mode) */
    mov $0x3F8, %edx
    mov $'S', %al
    out %al, %edx

    /* ============================================================
     * Set up AP stack
     * ============================================================
     * Use a larger stack area for the AP to support C function calls.
     */
    movq $0xFFFF, %rsp
    movq %rsp, %rbp

    /* Debug: Output 'Q' to confirm stack was set */
    mov $'Q', %al
    out %al, %edx

    /* Clear segment registers */
    mov $0, %ax
    mov %ax, %ss
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    /* Debug: Output 'A' to serial */
    mov $'A', %al
    out %al, %edx

    /* ============================================================
     * Load IDT from address
     * ============================================================
     * Use movabs to load the idt_ptr address directly.
     * The linker will resolve this at link time.
     */
    movabs $idt_ptr, %rcx             /* Load idt_ptr address directly */

    /* Debug: Output 'I' - before IDT load */
    mov $'I', %al
    out %al, %edx

    testq %rcx, %rcx                  /* Check if pointer is valid */
    jz skip_idt_load                  /* Skip if NULL (shouldn't happen) */
    lidt (%rcx)                       /* Load IDT - now exceptions can be handled */

    /* Debug: Output 'L' - after IDT load */
    mov $'L', %al
    out %al, %edx
skip_idt_load:

    /* Debug: Output 'T' - about to jump to ap_start */
    mov $'T', %al
    out %al, %edx

    /* Disable interrupts and do DIRECT jump to test_ap_start
     * NOTE: Indirect jump (jmp *%rax) doesn't work, must use direct jump */
    cli
    jmp test_ap_start  /* Direct jump to bridge function */

    /* Should never reach here */
    cli
    hlt
    jmp ap_trampoline_stub

    /* ============================================================
     * test_ap_start - Bridge function for AP startup
     * ============================================================
     * This function serves as a bridge from the trampoline to the C code.
     * It outputs a 'W' character to confirm the jump succeeded, then jumps
     * to the real ap_start function in C.
     *
     * NOTE: We use this bridge function because direct jumps to ap_start
     * from the trampoline work correctly, while indirect jumps (jmp *%rax)
     * have issues on some systems/QEMU configurations.
     */
    .global test_ap_start
test_ap_start:
    /* Output 'W' to confirm we reached the jump target */
    mov $0x3F8, %edx
    mov $'W', %al
    out %al, %edx

    /* Jump to the real ap_start function in C */
    jmp ap_start

    /* ============================================================
     * ap_start_placeholder - Runtime patchable address
     * ============================================================
     * The BSP will patch this 8-byte value with the actual address of
     * ap_start before starting the APs. This avoids GOT corruption issues.
     */
    .align 8
    .global ap_start_placeholder
ap_start_placeholder:
    .quad 0xDEADBEEFCAFEBABE  /* Patched by BSP with actual ap_start address */

    /* ============================================================
     * idt_ptr_placeholder - Runtime patchable IDT pointer
     * ============================================================
     * The BSP will patch this 8-byte value with the actual address of
     * idt_ptr before starting the APs.
     */
    .align 8
    .global idt_ptr_placeholder
idt_ptr_placeholder:
    .quad 0xDEADBEEFCAFEBABE  /* Patched by BSP with actual idt_ptr address */

    .size ap_trampoline_stub, . - ap_trampoline_stub

/* ============================================================
 * GOT (Global Offset Table) - Populated by linker at build time
 * ============================================================
 * This section contains pointers to kernel symbols that the trampoline needs.
 * The linker fills in the actual addresses during the link phase.
 * Each entry is 8 bytes (64-bit pointer).
 */
.section .ap_trampoline_got, "a"
.align 8
.global trampoline_got
trampoline_got:
    .quad boot_pml4      /* GOT[0]: PML4 page table address */
    .quad ap_start       /* GOT[1]: AP entry point function */
    .quad idt_ptr        /* GOT[2]: IDT pointer structure */
    .quad ap_stack_end   /* GOT[3]: AP stack end address */
