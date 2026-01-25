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

/* External symbols from C code
 * These are resolved at link time via the GOT in the trampoline */
.extern ap_start              /* AP entry point function */
.extern idt_ptr                /* IDT pointer structure in kernel BSS */
.extern boot_pml4              /* PML4 page table address */
.extern ap_stack_end           /* End of AP stack (top address) */

/* External placeholder symbols from AP trampoline
 * These are patched at runtime with the actual addresses */
.extern ap_start_placeholder   /* ap_start address placeholder in trampoline */
.extern idt_ptr_placeholder    /* idt_ptr address placeholder in trampoline */

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
.global ap_stack_bottom
.global ap_stack_end
ap_stack_bottom:
    .skip 16384  /* 16 KiB for APs */
ap_stack_end:

/* Page tables for Long Mode (must be aligned) */
.section .bss
.align 4096
.global boot_pml4
.global boot_pdpt
.global boot_pd
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
    movl $0, boot_pml4 + 4      /* High 32 bits */

    /* Set up PML4 entry 0x1FD -> points to PDPT (for high memory access)
     * Virtual address 0xFEE00000 requires PML4[0x1FD] because:
     * VA 0xFEE00000 = 0b11111110111000000000000000000000000
     * PML4 index = bits 39-47 = 0x1FD
     * This allows APIC at 0xFEE00000 to be accessed */
    movl $boot_pdpt, %eax
    orl $0x3, %eax              /* Present + Writable */
    movl %eax, boot_pml4 + 0x1FD*8  /* PML4[0x1FD] */
    movl $0, boot_pml4 + 0x1FD*8 + 4  /* High 32 bits */

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
     * For 0xFEE00000:
     *   PML4 index = (0xFEE00000 >> 39) & 0x1FF = 0x1FD
     *   PDPT index = (0xFEE00000 >> 30) & 0x1FF = 0x3 (NOT 0x1FC!)
     *   PD index   = (0xFEE00000 >> 21) & 0x1FF = 0x1F8 (504)
     *
     * Solution: Make PDPT[3] point to boot_pd
     * This creates the mapping: VA 0xFEE00000 -> PML4[0x1FD] -> PDPT[3] -> PD[504] -> PA 0xFEE00000
     */

    /* Set up PDPT[3] -> boot_pd for APIC access */
    movl $boot_pd, %eax
    orl $0x3, %eax              /* Present + Writable */
    movl %eax, boot_pdpt + 3*8  /* PDPT[3] */
    movl $0, boot_pdpt + 3*8 + 4  /* High 32 bits */

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

    /* IMPORTANT: Set up PD entry 503 AFTER the loop
     * The loop above overwrites all 512 entries including [503]
     * We need to map APIC at 0xFEE00000 in PD[503]
     * PD index = (0xFEE00000 >> 21) & 0x1FF = 503
     *
     * CRITICAL: For MMIO regions like APIC, we must disable caching
     * For strict UC (Uncacheable), we need: P=1, RW=1, CD=1, WT=1, PS=1
     * Do NOT use Global bit (bit 8) for MMIO regions!
     * 0x9B = 0b10011011 = P + RW + WT + CD + PS (strict UC)
     *
     * IMPORTANT: PD index for 0xFEE00000 is ((0xFEE00000 >> 21) & 0x1FF) = 503
     * NOT 504! This was a bug. */
    movl $0xFEE0009B, %eax     /* 0xFEE00000 | P + RW + WT + CD + PS for strict UC */
    movl %eax, boot_pd + 503*8  /* PD entry 503 (not 504!) - APIC MMIO region, uncached */
    movl $0, boot_pd + 503*8 + 4  /* High 32 bits */

    /* ============================================================
     * AP Trampoline is linked into kernel at physical 0x7000
     * ============================================================
     * No runtime patching needed - symbols resolved by linker via GOT.
     * The trampoline uses PIC with GOT-based addressing for all
     * kernel symbols (boot_pml4, ap_start, idt_ptr, ap_stack_end).
     */

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

    /* ============================================================
     * Patch AP Trampoline Placeholders
     * ============================================================
     * The GOT-based approach has a linker bug where addresses get corrupted.
     * Instead, we patch placeholders in the trampoline with the correct
     * addresses at runtime. The trampoline is at physical 0x7000.
     */
    /* Patch ap_start_placeholder with actual ap_start address */
    movabs $ap_start, %rax          /* Load actual ap_start address */
    mov %rax, ap_start_placeholder  /* Patch the placeholder */

    /* Patch idt_ptr_placeholder with actual idt_ptr address */
    movabs $idt_ptr, %rax           /* Load actual idt_ptr address */
    mov %rax, idt_ptr_placeholder   /* Patch the placeholder */

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
