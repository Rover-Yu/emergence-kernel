/* User program - verify ring 3 execution */

.section .text
.align 8                      /* 8-byte alignment for sysretq */
.global user_program_start
user_program_start:
    /* If we reach here in ring 3, we succeeded! */
    /* Infinite loop - CPU will execute this at ring 3 */
1:
    jmp 1b
