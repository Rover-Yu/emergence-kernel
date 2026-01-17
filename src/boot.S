/* JAKernel - Multiboot2 header and entry point */
.section .multiboot
.align 8

/* Multiboot2 Header */
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

/* Entry point */
.section .text
.global _start
.type _start, @function

_start:
    /* Disable interrupts */
    cli

    /* Set up stack (32-bit) */
    mov $stack_top, %esp

    /* Call kernel main */
    call kernel_main

    /* Halt if kernel returns */
halt:
    cli
    hlt
    jmp halt

.size _start, . - _start

/* Stack */
.section .bss
.align 16
stack_bottom:
    .skip 16384  /* 16 KiB stack */
stack_top:
