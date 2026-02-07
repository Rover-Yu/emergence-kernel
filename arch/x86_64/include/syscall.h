#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

/* GDT segment selectors (from gdt.h) */
#define GDT_KERNEL_CS   0x08
#define GDT_KERNEL_DS   0x10
#define GDT_USER_CS     0x18
#define GDT_USER_DS     0x20
#define GDT_TSS         0x28

/* Syscall numbers */
#define SYS_write       1
#define SYS_exit        2

/* Function prototypes */
void syscall_init(void);
void syscall_handler(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3);
void enter_user_mode(void);

#endif /* SYSCALL_H */
