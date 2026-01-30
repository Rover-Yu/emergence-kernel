/* Emergence Kernel - x86-64 serial port interface */

#ifndef EMERGENCE_ARCH_X86_64_SERIAL_H
#define EMERGENCE_ARCH_X86_64_SERIAL_H

/* COM1 I/O port base address */
#define SERIAL_COM1    0x3F8

/* Serial port functions */
void serial_putc(char c);
void serial_puts(const char *str);
void serial_put_hex(uint64_t value);
void serial_unlock(void);

#endif /* EMERGENCE_ARCH_X86_64_SERIAL_H */
