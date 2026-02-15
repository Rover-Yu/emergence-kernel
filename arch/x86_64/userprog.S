/* User program - verify ring 3 execution */

.section .rodata
user_msg:
    .asciz "RING3 OK"

.section .text
.align 8                      /* 8-byte alignment for sysretq */
.global user_program_start
user_program_start:
    /* We are now in ring 3! Make a syscall to verify. */

    /* sys_write(1, "RING3 OK", 8) */
    mov $1, %rax              /* SYS_write */
    mov $1, %rdi              /* fd = stdout */
    lea user_msg(%rip), %rsi  /* buf */
    mov $8, %rdx              /* count */
    syscall

    /* sys_exit(0) */
    mov $2, %rax              /* SYS_exit */
    mov $0, %rdi              /* exit_code = 0 */
    syscall

    /* Should not reach here, but loop if we do */
1:
    jmp 1b
