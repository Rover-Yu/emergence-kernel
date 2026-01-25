/* JAKernel - x86-64 serial port interface */

#ifndef JAKERNEL_ARCH_X86_64_SERIAL_H
#define JAKERNEL_ARCH_X86_64_SERIAL_H

/* COM1 I/O port base address */
#define SERIAL_COM1    0x3F8

/* Serial port functions */
void serial_putc(char c);
void serial_puts(const char *str);
void serial_unlock(void);

#endif /* JAKERNEL_ARCH_X86_64_SERIAL_H */
