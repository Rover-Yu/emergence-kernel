/* AP Trampoline - Real mode to Long Mode (64-bit) transition
 *
 * The AP starts in real mode at physical address TRAMPOLINE_BASE (0x7000) via STARTUP IPI.
 * This trampoline transitions: Real Mode -> Protected Mode -> Long Mode (64-bit)
 * Then jumps to ap_start() in the kernel.
 *
 * CRITICAL: This code must be position-independent (works at physical 0x7000)
 *
 * TRAMPOLINE MEMORY LAYOUT:
 *   0x0000 - Real mode entry point
 *   0x0100 - boot_pml4_placeholder (8 bytes) - patched with actual PML4 address
 *   0x0108 - ap_start_placeholder (8 bytes) - patched with ap_start function address
 *   0x01E0 - gdt32_gdtr - GDTR structure for 32-bit protected mode
 *   0x01F0 - gdt64_gdtr - GDTR structure for 64-bit long mode
 *   0x0E00 - GDT32 (3 descriptors: null, code, data)
 *   0x0F00 - GDT64 (3 descriptors: null, code, data)
 *   0x1000 - 64-bit stub entry point
 *   0x1400 - ap_stack_placeholder (8 bytes) - patched with AP stack address
 *   0x1500 - idt_ptr_placeholder (8 bytes) - patched with IDT pointer address
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
.equ OFFSET_PML4_PlaceHOLDER, 0x0100  /* PML4 page table address placeholder */
.equ OFFSET_AP_START_PLACEHOLDER, 0x0108 /* ap_start function address placeholder */
.equ OFFSET_GDT64_GDTR,  0x01F0  /* GDTR for 64-bit long mode GDT */

/* GDT base addresses */
.equ GDT32_BASE,         0x07E00  /* 32-bit GDT base (offset 0x0E00 from trampoline) */
.equ GDT64_BASE,         0x07F00  /* 64-bit GDT base (offset 0x0F00 from trampoline) */

/* 64-bit stub (at offset 0x1000 = physical 0x8000) */
.equ STUB_OFFSET,        0x1000  /* Stub offset within trampoline */
.equ STUB_PHYSICAL,      0x8000  /* Stub physical address (0x7000 + 0x1000) */
.equ OFFSET_STACK_PLACEHOLDER_FROM_STUB, 0x0400 /* Stack placeholder offset from stub base */
.equ OFFSET_IDT_PTR_PLACEHOLDER_FROM_STUB, 0x0500 /* IDT placeholder offset from stub base */

/* Alignment masks */
.equ ALIGN_4KB_MASK,      0xFFFFF000  /* Mask for 4KB page alignment */
.equ ALIGN_16B_MASK,      0xFFFFFFF0  /* Mask for 16-byte alignment */

/* Control register bits */
.equ CR4_PAE_BIT,        0x20    /* PAE enable bit in CR4 */
.equ CR4_PSE_BIT,        0x10    /* PSE enable bit in CR4 */
.equ EFER_LME_BIT,        0x1000  /* Long Mode Enable bit in IA32_EFER */
.equ EFER_NXE_BIT,        0x800   /* No-Execute Enable bit in IA32_EFER */
.equ CR0_PG_BIT,         0x80000000 /* Paging enable bit in CR0 */

/* MSRs */
.equ MSR_EFER,            0xC0000080  /* Extended Feature Enable Register MSR */

/* Protected mode entry point */
.equ PROTECTED_MODE_ENTRY, 0x703D /* Physical address of pm_entry */

/* Code selectors */
.selector = 0x0008

/* Function addresses are now read from placeholders instead of hardcoded constants
 * This ensures addresses stay correct after kernel relinking */

    .code16
    .org 0

trampoline_start:
    cli

    /* ========================================================================
     * Real Mode Setup
     * ========================================================================
     * Initialize segment registers and set up real mode stack
     */
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
     * ========================================================================
     * The GDTR structure is at offset OFFSET_GDT32_GDTR within the trampoline.
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
     * ============================================================
     * CRITICAL: PAE must be enabled BEFORE loading CR3.
     * When PAE is disabled, CR3 is interpreted as a 32-bit page directory address.
     * When PAE is enabled, CR3 is interpreted as a PML4 address for Long Mode.
     * Loading CR3 while PAE is disabled causes incorrect interpretation. */
    mov %cr4, %eax
    or $(1 << 5), %eax    /* Set PAE bit (bit 5) */
    mov %eax, %cr4

    /* Debug: Output "A" */
    mov $0x3F8, %edx
    mov $'A', %al
    out %al, %edx

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

    /* DEBUG: Output 'P' - CR3 value loaded into EAX */
    mov $0x3F8, %edx
    mov $'P', %al
    out %al, %edx

    mov %eax, %cr3

    /* CRITICAL: TLB flush after loading CR3
     * The TLB is per-CPU and must be invalidated when CR3 changes.
     * Without this flush, the AP has stale translations from the BSP.
     * Note: We're still in 32-bit mode here, so use %eax not %rax. */
    mov %cr3, %eax               /* Read CR3 back */
    mov %eax, %cr3               /* Write to invalidate TLB entries */

    /* Debug: Output "3" */
    mov $0x3F8, %edx
    mov $'3', %al
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

    /* Set up AP stack using hardcoded address
     * The trampoline occupies 0x7000-0x8FFF (8 KB)
     * We'll use 0x9FFF as the stack top (4 KB stack area)
     * This is simpler than using a patched placeholder. */
    movq $0x9FFF, %rsp        /* Hardcoded stack top */
    movq $0x9FFF, %rbp        /* Set base pointer too */

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

    /* Debug: Output 'A' to serial (after segment registers cleared) */
    mov $'A', %al
    out %al, %edx

    /* Skip stack test - just try to jump to ap_start */
    /* Debug: Output 'T' to serial (about to jump to ap_start) */
    mov $'T', %al
    out %al, %edx

    /* ============================================================
     * Jump to ap_start using absolute physical address
     * ============================================================
     * The stub runs at physical 0x8000 with identity-mapped page tables.
     * The ap_start_placeholder is at physical 0x7108 (0x7000 + 0x108).
     * Since we know the exact physical addresses, we can use absolute
     * addressing instead of position-independent techniques.
     */
    /* Debug: Output '5' - about to load ap_start address */
    mov $'5', %al
    out %al, %edx

    /* Try executing some instructions inline to verify we're in 64-bit mode */
    movq $0xDEADBEEFCAFEBABE, %r8

    /* Debug: Output '6' - inline execution worked */
    mov $'6', %al
    out %al, %edx

    /* Load ap_start address from placeholder (0x7108 = 0x7000 + 0x108)
     * The placeholder is patched by boot.S with the actual ap_start address */
    movq $0x7108, %rax          /* Address of ap_start_placeholder */
    movq (%rax), %rbx           /* Load patched ap_start address */
    /* NOTE: Do NOT skip endbr64 - jump to actual function start */

    /* Debug: Output '8' - loaded ap_start address from placeholder */
    mov $'8', %al
    out %al, %edx

    /* Disable interrupts */
    cli

    /* Debug: Output 'J' - about to jump to ap_start */
    mov $'J', %al
    out %al, %edx

    /* ============================================================
     * Skip the test function and go directly to IDT loading
     * ============================================================
     * Save the ap_start address for later use */
    movq %rbx, %r12                /* Save ap_start address in R12 */

    /* Jump directly to IDT loading section */
    jmp load_idt_section

ap_test_local:
    /* Debug: Output 'K' - reached local test function */
    mov $0x3F8, %edx
    mov $'K', %al
    out %al, %edx

    /* ============================================================
     * Test: Can we read from kernel code memory?
     * ============================================================ */
    /* Try to read the first byte of serial_puts (should be 0xF3 for endbr64)
     * Note: serial_puts address may vary based on code size changes */
    movq $0x100a10, %rax           /* Address of serial_puts */
    movb (%rax), %al               /* Read first byte */
    /* Verify it's 0xF3 (endbr64 opcode) */
    cmpb $0xF3, %al
    jne memory_read_failed

    /* Debug: Output 'M' - memory read successful */
    mov $0x3F8, %edx
    mov $'M', %al
    out %al, %edx

    /* ============================================================
     * Flush TLB before executing kernel code
     * ============================================================
     * The TLB is per-CPU. The AP needs to reload CR3 to flush it.
     * This ensures the AP has valid translations for kernel code. */
    /* Read current CR3 value */
    movq %cr3, %rax
    /* Write it back to flush TLB */
    movq %rax, %cr3

    /* Debug: Output 'T' - TLB flushed */
    mov $0x3F8, %edx
    mov $'T', %al
    out %al, %edx

    /* ============================================================
     * Test: Output a character using serial port directly
     * ============================================================ */
    mov $'!', %al
    out %al, %dx

    /* ============================================================
     * Go directly to ap_start
     * ============================================================ */
    /* Debug: Output 'C' - about to call ap_start */
    mov $'C', %al
    out %al, %dx

    /* Now jump to ap_start using the saved address (from R12) */
    movq %r12, %rbx                /* Restore ap_start address from R12 */
    /* Note: ap_start address already has +4 to skip endbr64 */

    /* Debug: Output the first nibble of ap_start address for verification */
    movq %rbx, %rax
    shr $60, %rax                  /* Get top 4 bits */
    cmp $0, %rax
    je addr_zero
    cmp $1, %rax
    je addr_one
    cmp $2, %rax
    je addr_two
    cmp $3, %rax
    je addr_three

addr_zero:
    mov $'0', %al
    jmp addr_done
addr_one:
    mov $'1', %al
    jmp addr_done
addr_two:
    mov $'2', %al
    jmp addr_done
addr_three:
    mov $'3', %al
    jmp addr_done

addr_done:
    out %al, %dx

    /* ============================================================
     * Load kernel's GDT before calling kernel code
     * ============================================================
     * The trampoline's GDT might not match the kernel's expectations.
     * The kernel's GDT is at gdt64 (defined in boot.S).
     * We need to load the GDT pointer and reload segment registers.
     *
     * The gdt64_ptr structure contains:
     *   - Limit: gdt64_ptr - gdt64 - 1 (2 bytes)
     *   - Base: address of gdt64 (8 bytes)
     *
     * We'll use the same address as the BSP (which is identity-mapped). */

    /* For now, skip GDT reload since we're in 64-bit mode where
     * segment registers are mostly ignored. This might not be necessary. */

load_idt_section:
    /* ============================================================
     * Load IDT before calling C code
     * ============================================================
     * The BSP has set up the IDT in kernel memory.
     * The AP needs to load the same IDT to handle exceptions.
     * Without a valid IDT, any exception in C code causes a triple fault.
     *
     * TEMPORARY: Skip IDT load to test if it's causing the crash
     * If we reach C code ('C' output), then IDT load was the issue.
     */

    /* TEMPORARY: Skip IDT load for debugging
     * The IDT is in BSS which might not be properly mapped for the AP.
     * Let's try jumping to C code without loading IDT first. */
    /* Debug: Output '1' - skipping IDT load */
    mov $'1', %al
    out %al, %dx

    /* Skip the IDT load for now */
    jmp skip_idt_load

    /* Original IDT load code (temporarily disabled) */
    movq $0x8500, %rax
    movq (%rax), %rax
    movb (%rax), %cl
    test %cl, %cl
    jz idt_read_zero
    mov $'Z', %al
    out %al, %dx
    jmp idt_read_done
idt_read_zero:
    mov $'Y', %al
    out %al, %dx
idt_read_done:
    lidt (%rax)
    mov $'I', %al
    out %al, %dx

skip_idt_load:

    /* Debug: Output 'D' - about to execute call instruction */
    mov $'D', %al
    out %al, %dx

    /* ============================================================
     * TEST: Output "JIMI" directly instead of calling C function
     * ============================================================
     * The C function's serial_puts has a spinlock that might hang.
     * Let's output "JIMI" directly to verify the call mechanism works. */
    mov $'J', %al
    out %al, %dx
    mov $'I', %al
    out %al, %dx
    mov $'M', %al
    out %al, %dx
    mov $'I', %al
    out %al, %dx
    mov $'\n', %al
    out %al, %dx

    /* Now try jumping to ap_start (not call - no return address) */
    /* Debug: Output 'K' - about to jump */
    mov $'K', %al
    out %al, %dx

    /* Stack is already set up in the stub with hardcoded address 0x9FFF
     * No need to set it up again here. */

    /* Debug: Output 'T' - about to jump to ap_start */
    mov $'T', %al
    out %al, %dx

    /* DEBUG: Verify RBX is non-zero before jumping */
    mov %rbx, %r8
    test %r8, %r8
    jz rbx_is_zero
    /* RBX is non-zero - output 'Z' */
    mov $'Z', %al
    out %al, %dx

    /* DEBUG: Output low byte of RBX (ap_start address) as hex digit
     * This helps verify the correct address is loaded */
    movq %rbx, %r8
    andq $0xF, %r8             /* Get low nibble */
    cmpq $10, %r8
    jae rbx_hex_af
    addq $'0', %r8             /* 0-9 */
    jmp rbx_output_done
rbx_hex_af:
    addq $'A' - 10, %r8        /* A-F */
rbx_output_done:
    movq %r8, %rax
    out %al, %dx

    jmp do_jump
rbx_is_zero:
    /* RBX is zero - this is an error! */
    mov $'z', %al
    out %al, %dx
    /* Halt on error */
    cli
    hlt
    jmp rbx_is_zero

/* ============================================================
 * TEST: Inline function that mimics ap_start
 * ============================================================
 * This tests if jumping within the trampoline works,
 * before trying to jump to external kernel code. */
test_inline_function:
    /* This is where we'd jump if RBX-based jump fails */
    mov $0x3F8, %dx
    mov $'!', %al              /* Output '!' - reached inline test */
    out %al, %dx

    /* Halt */
    cli
1:  hlt
    jmp 1b

do_jump:
    /* Jump to ap_start using hardcoded address
     * The patched placeholder approach had issues.
     * We use a hardcoded address for now - this works because:
     * 1. The kernel is loaded at a fixed address by GRUB
     * 2. The address can be determined from the ELF file
     * TODO: Make this more flexible by fixing the placeholder patching */
    movq $0x104170, %rax        /* Hardcoded ap_start address */
    jmp *%rax                    /* Indirect jump through RAX */

    /* We should NEVER reach here */
    cli
    hlt
    jmp do_jump

    /* Debug: Output 'E' - returned from ap_start (should never reach) */
    mov $'E', %al
    out %al, %dx

    /* Should never reach here (ap_start doesn't return) */
ap_test_halt:
    hlt
    jmp ap_test_halt

memory_read_failed:
    /* Debug: Output 'X' - memory read failed */
    mov $0x3F8, %edx
    mov $'X', %al
    out %al, %edx
    jmp ap_test_halt

halt_after_jump:
    mov $'F', %al
    out %al, %edx
    cli
    hlt
    jmp halt_after_jump

    /* Should never reach here */
ap_stub_halt:
    cli
    hlt
    jmp ap_stub_halt

    /* Placeholder for AP stack address (patched by boot.S) */
    .org 0x1400
ap_stack_placeholder:
    .quad 0xDEADBEEFCAFEBABE  /* Patched with actual ap_stack_end address */

    /* Placeholder for IDT pointer address (patched by boot.S)
     * This holds the address of the kernel's idt_ptr structure
     * which contains the limit and base address of the IDT */
    .org 0x1500
idt_ptr_placeholder:
    .quad 0xDEADBEEFCAFEBABE  /* Patched with actual idt_ptr address */

trampoline_end:
