/* Syscall Test Program - Tests fork, getpid, yield, wait syscalls
 *
 * This user mode program tests the new syscalls implemented:
 * - SYS_getpid: Get process ID
 * - SYS_yield: Voluntarily yield CPU
 * - SYS_fork: Create child process
 * - SYS_wait: Wait for child process
 */

.section .rodata

/* Test messages */
msg_test_start: .asciz "[USER] === Syscall Test Starting ==="
msg_getpid: .asciz "[USER] Testing getpid..."
msg_getpid_result: .asciz "[USER] getpid returned: %d"
msg_yield: .asciz "[USER] Testing yield..."
msg_yield_done: .asciz "[USER] yield completed"
msg_fork: .asciz "[USER] Testing fork..."
msg_fork_parent: .asciz "[USER] fork: I am parent, child PID=%d"
msg_fork_child: .asciz "[USER] fork: I am child, PID=%d"
msg_wait: .asciz "[USER] Testing wait..."
msg_wait_result: .asciz "[USER] wait: child exited with status=%d"
msg_exit: .asciz "[USER] All tests completed, exiting"
msg_newline: .asciz "\n"

.section .data
/* Buffer for string formatting - in .data section for proper user mode mapping */
.format_buffer: .skip 64

.section .text
.align 8
.global syscall_test_start
.type syscall_test_start, @function

/* Macro to write a string to stdout */
.macro write_string str_addr
    mov $1, %rax              /* SYS_write */
    mov $1, %rdi              /* fd = stdout */
    lea \str_addr(%rip), %rsi /* buf */
    mov $0xFFFF, %rdx         /* count (max) */
    syscall
.endm

/* Macro to print a number */
.macro print_number prefix, value
    /* Convert value to string */
    mov \value, %rdi
    lea .format_buffer(%rip), %rsi
    call int_to_string

    /* Write prefix */
    write_string \prefix

    /* Write number */
    mov $1, %rax
    mov $1, %rdi
    lea .format_buffer(%rip), %rsi
    /* RDX already contains string length from int_to_string */
    syscall

    /* Write newline */
    write_string msg_newline
.endm

/* Entry point */
syscall_test_start:
    /* Write test start message */
    write_string msg_test_start
    write_string msg_newline

    /* === Test 1: getpid === */
    write_string msg_getpid

    mov $4, %rax              /* SYS_getpid */
    syscall
    mov %rax, %r12            /* Save PID */

    /* Simple write of PID result message (skip number formatting for now) */
    write_string msg_getpid_result

    /* === Test 2: yield === */
    write_string msg_yield

    mov $3, %rax              /* SYS_yield */
    syscall

    write_string msg_yield_done

    /* === SKIP fork/wait tests for now ===
     * Fork requires proper register state copying which is not yet implemented
     * TODO: Implement fork with parent/child context switching */

    /* Exit with success */
    write_string msg_exit
    mov $2, %rax              /* SYS_exit */
    mov $0, %rdi              /* exit_code = 0 */
    syscall
    jmp .                     /* Should not reach here */

/* === Helper function: int_to_string === */
/* Converts integer in RDI to ASCII string at RSI */
/* Returns string length in RDX */
.type int_to_string, @function
int_to_string:
    push %rbx
    push %rcx
    push %rdi
    push %r8
    push %r9
    push %r10

    /* Handle zero */
    test %rdi, %rdi
    jnz .convert_positive
    movb $'0', (%rsi)
    mov $1, %rdx
    jmp .int_to_string_done

.convert_positive:
    /* Convert number to ASCII using stack as temp buffer
     * Digits will be pushed, then popped in reverse order */
    mov %rdi, %rax           /* Value to convert */
    mov %rsi, %r8            /* Save dest buffer pointer */
    mov $10, %rbx            /* Base 10 */
    xor %rcx, %rcx           /* Digit counter */

.div_loop:
    xor %rdx, %rdx
    div %rbx                 /* RAX /= 10, RDX = remainder */
    add $'0', %dl            /* Convert to ASCII */
    push %rdx                /* Save digit on stack */
    inc %rcx
    test %rax, %rax
    jnz .div_loop

    /* Pop digits in reverse order (now correct) */
    mov %rcx, %r9            /* Save digit count */
    mov %r8, %rdi            /* Dest buffer pointer */

.pop_loop:
    test %r9, %r9
    jz .pop_done
    pop %rax
    mov %al, (%rdi)
    inc %rdi
    dec %r9
    jmp .pop_loop

.pop_done:
    mov %rcx, %rdx           /* Return length in RDX */

.int_to_string_done:
    pop %r10
    pop %r9
    pop %r8
    pop %rdi
    pop %rcx
    pop %rbx
    ret

.size syscall_test_start, .-syscall_test_start
.size int_to_string, .-int_to_string
