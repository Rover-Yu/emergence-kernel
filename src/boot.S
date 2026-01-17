/* JAKernel - Multiboot2 header and entry point */

/* Multiboot2 Header */
.section .multiboot
.align 8

multiboot_header_start:
    /* magic */
    .long 0xe85250d6
    /* architecture: i386 */
    .long 0
    /* header length */
    .long multiboot_header_end - multiboot_header_start
    /* checksum */
    .long -(0xe85250d6 + 0 + (multiboot_header_end - multiboot_header_start))

    /* End tag type */
    .short 0
    .short 0
    .long 8
multiboot_header_end:

/* Boot stack (temporary, for 32-bit code) */
.section .bss
.align 16
stack_bottom:
    .skip 16384  /* 16 KiB stack */
stack_top:

/* Page tables for Long Mode (must be aligned) */
.section .bss
.align 4096
boot_pml4:
    .skip 4096
boot_pdpt:
    .skip 4096
boot_pd:
    .skip 4096

/* GDT for 64-bit mode */
.section .rodata
.align 16
gdt64:
    .quad 0                                        /* Null descriptor */
    .quad 0x00209A0000000000                       /* 64-bit code segment (exec/read, 64-bit) */
    .quad 0x0000920000000000                       /* 64-bit data segment (read/write) */
gdt64_ptr:
    .word gdt64_ptr - gdt64 - 1
    .quad gdt64

/* Entry point - 32-bit bootstrap code */
.section .text
.global _start
.type _start, @function

.code32
_start:
    /* Disable interrupts */
    cli

    /* Set up temporary stack */
    mov $stack_top, %esp

    /* Save Multiboot info pointer (passed in ebx) */
    mov %ebx, %esi

    /* ============================================================
     * MMU Initialization for x86-64 Long Mode
     * ============================================================
     * Long Mode requires:
     * 1. CPUID support (to detect long mode capability)
     * 2. Long Mode support (CPU feature bit 29 in EDX)
     * 3. PAE (Physical Address Extension) enabled
     * 4. Page tables set up (4-level paging: PML4 -> PDPT -> PD -> PT)
     * 5. LME bit set in EFER MSR
     * 6. Paging enabled (CR0.PG)
     * ============================================================ */

    /* ---- CPU Feature Detection ---- */
    /* Check if CPUID instruction is supported by toggling EFLAGS bit 21 */
    pushfl
    popl %eax
    movl %eax, %ecx
    xorl $(1 << 21), %eax       /* Flip CPUID bit in EFLAGS copy */
    pushl %eax
    popfl
    pushfl
    popl %eax
    pushl %ecx
    popfl
    cmpl %ecx, %eax             /* If bit didn't change, CPUID not supported */
    je no_long_mode
    jz no_long_mode

    /* Check for Long Mode support via CPUID */
    mov $0x80000000, %eax       /* Query max extended function */
    cpuid
    cmpl $0x80000001, %eax      /* Need function 0x80000001 for feature bits */
    jb no_long_mode

    mov $0x80000001, %eax       /* Get extended processor info and feature bits */
    cpuid
    testl $(1 << 29), %edx      /* Test LM bit (bit 29) - Long Mode available? */
    jz no_long_mode

    /* ---- Page Table Setup (4-Level Paging for x86-64) ----
     * x86-64 uses 4-level page tables:
     * - PML4 (Page Map Level 4)    : Maps 256 TB of address space
     * - PDPT (Page Directory Pointer): Maps 512 GB
     * - PD   (Page Directory)        : Maps 1 GB
     * - PT   (Page Table)            : Maps 2 MB (not used, we use 2MB pages)
     *
     * For simplicity, we identity-map the first 2MB of physical memory.
     * This means virtual address 0x0 maps to physical address 0x0.
     */

    /* Set up PML4 entry 0 -> points to PDPT
     * Entry format: [63:52 reserved][51:12 PDPT phys addr][11:2 flags][1:0 reserved]
     * Flags: bit 0 = Present, bit 1 = Writable */
    movl $boot_pdpt, %eax
    orl $0x3, %eax              /* Set Present (bit 0) and Writable (bit 1) */
    movl %eax, boot_pml4

    /* Set up PDPT entry 0 -> points to Page Directory
     * Same format as PML4 entry */
    movl $boot_pd, %eax
    orl $0x3, %eax              /* Present + Writable */
    movl %eax, boot_pdpt

    /* Set up PD entry 0 -> identity map first 2MB using a 2MB page
     * Entry format with PS bit set: [63:52 reserved][51:21 reserved][20:13 PAT][12:0 flags]
     * - Physical address must be 2MB aligned (bits 20-0 are zero)
     * - Bit 7 (PS) = Page Size: 1 = 2MB page, 0 = 4KB page
     * - Flags: Present (bit 0), Writable (bit 1), PS/huge (bit 7) */
    movl $0x83, %eax            /* 0x83 = Present(1) | Writable(2) | PS(0x80) */
    movl %eax, boot_pd

    /* ---- CR Register Configuration ---- */
    /* Load PML4 base address into CR3
     * CR3 contains the physical address of the PML4 table
     * Must be 4KB aligned (bits 11-0 are reserved) */
    movl $boot_pml4, %ecx
    movl %ecx, %cr3

    /* Enable PAE (Physical Address Extension)
     * CR4.PAE (bit 5) enables 36-bit physical addresses and 3-level paging
     * PAE is required for Long Mode support */
    movl %cr4, %eax
    orl $(1 << 5), %eax         /* Set PAE bit (bit 5) */
    movl %eax, %cr4

    /* ---- Enable Long Mode ----
     * Long Mode is enabled by setting bit 8 (LME) in the EFER MSR
     * EFER (Extended Feature Enable Register) is MSR 0xC0000080 */
    movl $0xC0000080, %ecx      /* EFER MSR number */
    rdmsr                       /* Read MSR: EDX:EAX = MSR[ECX] */
    orl $(1 << 8), %eax         /* Set LME bit (bit 8) - Long Mode Enable */
    wrmsr                       /* Write MSR: MSR[ECX] = EDX:EAX */

    /* ---- Enable Paging ----
     * CR0.PG (bit 31) enables virtual memory paging
     * Once both LME and PG are set, the CPU transitions to Long Mode
     * IMPORTANT: Must be enabled AFTER LME is set */
    movl %cr0, %eax
    orl $(1 << 31), %eax        /* Set PG bit (bit 31) - enable paging */
    movl %eax, %cr0

    /* Load GDT */
    lgdt gdt64_ptr

    /* Far jump to 64-bit code */
    ljmp $8, $long_mode_start

    /* Halt if CPU doesn't support long mode */
no_long_mode:
    cli
halt_loop:
    hlt
    jmp halt_loop

.size _start, . - _start

/* 64-bit code section */
.section .text
.code64
.global long_mode_start
.type long_mode_start, @function

long_mode_start:
    /* Set up 64-bit stack */
    mov $stack_top, %rsp

    /* Clear segment registers */
    mov $0, %ax
    mov %ax, %ss
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    /* Call kernel main */
    call kernel_main

    /* Halt if kernel returns */
halt:
    cli
    hlt
    jmp halt

.size long_mode_start, . - long_mode_start
