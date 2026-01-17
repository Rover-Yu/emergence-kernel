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

/* BSP initialization flag - APs wait until BSP sets this */
.section .bss
.align 8
.global bsp_init_done
bsp_init_done:
    .skip 8  /* 64-bit flag */

/* CPU boot counter - atomic increment to determine BSP/AP */
.section .bss
.align 8
.global cpu_boot_counter
cpu_boot_counter:
    .skip 8  /* 64-bit counter */

/* Boot stack (temporary, for 32-bit code) */
.section .bss
.align 16
stack_bottom:
    .skip 16384  /* 16 KiB stack */
stack_top:

/* Page tables for APIC mapping */
.section .bss
.align 4096
.global boot_pd_apic
boot_pd_apic:
    .skip 4096

.section .bss
.align 4096
.global boot_pt_apic
boot_pt_apic:
    .skip 4096

/* AP stacks (one for each AP) */
.section .bss
.align 16
ap_stack_bottom:
    .skip 16384  /* 16 KiB for APs */
ap_stack_end:

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

    /* Check if this CPU is first to run (BSP) or an AP
     * We use a simple atomic increment on cpu_boot_counter */
    mov $cpu_boot_counter, %eax
    lock incl (%eax)           /* Atomic increment */
    mov (%eax), %eax           /* Get new value */
    cmp $1, %eax               /* If 1, we are BSP */
    je boot_is_bsp

    /* ============================================================
     * AP Path - Wait for BSP to complete initialization
     * ============================================================ */
ap_wait_loop:
    mov $bsp_init_done, %eax
    mov (%eax), %eax            /* Load flag value (32-bit load) */
    test %eax, %eax             /* Check if BSP is done */
    jz ap_wait_loop             /* If 0, keep waiting */

    /* BSP is done, AP continues to AP entry point */
    jmp ap_entry

boot_is_bsp:
    /* ============================================================
     * BSP Path - Boot the system
     * ============================================================ */

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
     * We identity-map the first 1GB of physical memory using 2MB pages.
     */

    /* Set up PML4 entry 0 -> points to PDPT */
    movl $boot_pdpt, %eax
    orl $0x3, %eax              /* Set Present (bit 0) and Writable (bit 1) */
    movl %eax, boot_pml4

    /* Set up PDPT entry 0 -> points to Page Directory */
    movl $boot_pd, %eax
    orl $0x3, %eax              /* Present + Writable */
    movl %eax, boot_pdpt

    /* ============================================================
     * APIC Memory Mapping Strategy
     * ============================================================
     * For 0xFEE00000 access, we need PDPT[3] -> PD[504]
     *
     * Challenge: boot_pd only has 512 entries (indices 0-511)
     * We need to access PD[504], which is in range
     * BUT: The virtual address 0xFEE00000 translates to PDPT[3], NOT PDPT[0]
     *
     * Solution: Make PDPT[3] also point to boot_pd
     * This means addresses 3GB-4GB will use the same PD entries as 0-1GB
     * For 0xFEE00000: PDPT[3] -> boot_pd[504] -> should work!
     */

    /* Set up PDPT[3] -> boot_pd for APIC access */
    movl $boot_pd, %eax
    orl $0x3, %eax              /* Present + Writable */
    movl %eax, boot_pdpt + 3*8  /* PDPT[3] */
    movl $0, boot_pdpt + 3*8 + 4  /* High 32 bits */

    /* Now set up boot_pd[504] to map the APIC region
     * Note: This will be done AFTER the loop that sets up the first 1GB
     * For now, let's set up PD[504] here before the loop */

    /* Set up PD entry 504 -> 2MB page at 0xFEC00000 (covers APIC at 0xFEE00000) */
    movl $0xFEC00183, %eax     /* 0xFEC00000 | Present | Writable | PS (2MB) */
    movl %eax, boot_pd + 504*8  /* PD entry 504 */
    movl $0, boot_pd + 504*8 + 4  /* High 32 bits */

    /* Set up PD entries for first 1GB using 2MB pages (512 entries needed) */
    leal boot_pd, %esi         /* PD base address */
    movl $512, %ecx            /* 512 entries for 1GB */
    xorl %edx, %edx            /* Running physical address */

1:
    movl %edx, %eax
    orl $0x183, %eax           /* Present | Writable | PS (2MB page) */
    movl %eax, (%esi)          /* Store low 32 bits of PD entry */
    movl $0, 4(%esi)           /* Store high 32 bits (reserved/zero) */
    addl $8, %esi              /* Next PD entry (8 bytes per entry) */
    addl $0x200000, %edx       /* Next 2MB physical address */
    loop 1b                    /* Loop for all 512 entries */

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
    /* Note: bsp_init_done will be set by kernel code when ready */
    call kernel_main

    /* Halt if kernel returns */
halt:
    cli
    hlt
    jmp halt

.size long_mode_start, . - long_mode_start

/* AP entry point - called when BSP initialization is complete */
.global ap_entry
.code32
ap_entry:
    /* APs skip to 64-bit mode setup since BSP already did it */
    /* Load GDT */
    lgdt gdt64_ptr

    /* Far jump to 64-bit code */
    ljmp $8, $ap_mode_64

.code64
ap_mode_64:
    /* Set up AP stack (use separate stack area) */
    mov $ap_stack_end, %rsp

    /* Clear segment registers */
    mov $0, %ax
    mov %ax, %ss
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    /* Call AP start function in C */
    movq $ap_start, %rax
    call *%rax

    /* Halt if ap_start returns */
ap_halt:
    cli
    hlt
    jmp ap_halt

/* ============================================================================
 * AP (Application Processor) Trampoline Code
 * ============================================================================
 * This code is placed at physical address 0x7000 and is executed by APs
 * when they are woken up by STARTUP IPI. It starts in real mode and
 * switches to long mode before calling ap_start().
 * ============================================================================ */

.section .ap_trampoline
.align 4096
.global ap_trampoline_start
ap_trampoline_start:
    /* Real mode entry point for APs */
    .code16
    cli

    /* Load a temporary GDT */
    lgdt gdt32_ptr - ap_trampoline_start + 0x7000

    /* Enable protected mode */
    movl %cr0, %eax
    orl $1, %eax
    movl %eax, %cr0

    /* Far jump to 32-bit protected mode */
    ljmp $0x08, $ap_protected_mode - ap_trampoline_start + 0x7000

    /* 32-bit protected mode code */
    .code32
ap_protected_mode:
    /* Set up data segments */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss

    /* Set up temporary stack */
    movl $ap_stack_top - ap_trampoline_start + 0x7000, %esp

    /* Enable PAE */
    movl %cr4, %eax
    orl $(1 << 5), %eax
    movl %eax, %cr4

    /* Use same page tables as BSP (identity mapping) */
    movl $boot_pml4, %ecx
    movl %ecx, %cr3

    /* Enable long mode */
    movl $0xC0000080, %ecx
    rdmsr
    orl $(1 << 8), %eax
    wrmsr

    /* Enable paging */
    movl %cr0, %eax
    orl $(1 << 31), %eax
    movl %eax, %cr0

    /* Load 64-bit GDT */
    lgdt gdt64_ptr - ap_trampoline_start + 0x7000

    /* Far jump to 64-bit mode */
    ljmp $0x08, $_trampoline_long_mode - ap_trampoline_start + 0x7000

    /* 64-bit long mode code */
    .code64
_trampoline_long_mode:
    /* Clear segment registers */
    movq $0, %rax
    mov %ax, %ss
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    /* Call AP start function in C */
    movq $ap_start, %rax
    call *%rax

    /* Halt if ap_start returns */
_trampoline_halt:
    cli
    hlt
    jmp _trampoline_halt

/* ============================================================================
 * AP GDT and Data Structures
 * ============================================================================ */

/* 32-bit GDT for AP (protected mode) */
.align 16
ap_gdt32:
    .quad 0                           /* Null descriptor */
    .quad 0x00CF9A000000FFFF          /* 32-bit code segment */
    .quad 0x00CF92000000FFFF          /* 32-bit data segment */
gdt32_ptr:
    .word gdt32_ptr - ap_gdt32 - 1
    .quad ap_gdt32 - ap_trampoline_start + 0x7000

/* 64-bit GDT for AP (long mode) - reuse BSP's GDT layout */
.align 16
ap_gdt64:
    .quad 0                           /* Null descriptor */
    .quad 0x00209A0000000000          /* 64-bit code segment */
    .quad 0x0000920000000000          /* 64-bit data segment */
ap_gdt64_ptr:
    .word ap_gdt64_ptr - ap_gdt64 - 1
    .quad ap_gdt64 - ap_trampoline_start + 0x7000

/* AP temporary stack */
.align 16
.skip 4096
ap_stack_top:

.size ap_trampoline_start, . - ap_trampoline_start

/* External functions */
.global ap_start
.global boot_pml4
.global ap_trampoline_end
ap_trampoline_end:
