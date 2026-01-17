/* JAKernel - x86-64 serial port (COM1) driver */

#include "arch/x86_64/serial.h"
#include "arch/x86_64/io.h"

/* Initialize COM1 serial port for 115200 baud, 8N1 */
void serial_init(void) {
    /* Disable interrupts */
    outb(SERIAL_COM1 + 1, 0x00);

    /* Enable DLAB (Divisor Latch Access Bit) to set baud rate */
    outb(SERIAL_COM1 + 3, 0x80);

    /* Set baud rate divisor to 1 (115200 baud with base clock 115200*1 = 115200) */
    outb(SERIAL_COM1 + 0, 0x01);    /* Low byte */
    outb(SERIAL_COM1 + 1, 0x00);    /* High byte */

    /* 8 bits, no parity, one stop bit, disable DLAB */
    outb(SERIAL_COM1 + 3, 0x03);

    /* Enable FIFO, clear buffers, 14-byte threshold */
    outb(SERIAL_COM1 + 2, 0xC7);

    /* Enable IRQs, set RTS/DSR */
    outb(SERIAL_COM1 + 4, 0x0B);
}

/* Write a single character to COM1 */
void serial_putc(char c) {
    /* Wait for transmit hold register to be empty (bit 5 of LSR) */
    while ((inb(SERIAL_COM1 + 5) & 0x20) == 0)
        ;

    outb(SERIAL_COM1, c);
}

/* Write a null-terminated string to COM1 */
void serial_puts(const char *str) {
    while (*str) {
        serial_putc(*str);
        str++;
    }
}
