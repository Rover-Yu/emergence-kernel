/* JAKernel - Main kernel code */

#include <stdint.h>

/* VGA text mode buffer */
#define VGA_BUFFER 0xB8000
#define VGA_WIDTH 80
#define VGA_HEIGHT 25

/* Colors */
#define VGA_COLOR_BLACK 0
#define VGA_COLOR_WHITE 15

/* Serial port constants */
#define SERIAL_COM1 0x3F8

/* Serial port functions */
static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static void serial_init() {
    /* Disable interrupts */
    outb(SERIAL_COM1 + 1, 0x00);
    /* Set baud rate divisor to 1 (115200 baud) */
    outb(SERIAL_COM1 + 3, 0x80);
    outb(SERIAL_COM1 + 0, 0x01);
    outb(SERIAL_COM1 + 1, 0x00);
    /* 8 bits, no parity, one stop bit */
    outb(SERIAL_COM1 + 3, 0x03);
    /* Enable FIFO, clear, 14-byte threshold */
    outb(SERIAL_COM1 + 2, 0xC7);
    /* IRQs enabled, RTS/DSR set */
    outb(SERIAL_COM1 + 4, 0x0B);
}

static void serial_putc(char c) {
    /* Wait for transmit buffer empty */
    while ((inb(SERIAL_COM1 + 5) & 0x20) == 0);
    outb(SERIAL_COM1, c);
}

static void serial_puts(const char *str) {
    while (*str) {
        serial_putc(*str);
        str++;
    }
}

/* VGA functions */
static void vga_putc(char c, int row, int col, uint8_t color) {
    uint16_t *vga = (uint16_t *)VGA_BUFFER;
    vga[row * VGA_WIDTH + col] = (uint16_t)((color << 8) | c);
}

static void vga_puts(const char *str, int row, int col, uint8_t color) {
    while (*str) {
        vga_putc(*str, row, col, color);
        col++;
        str++;
    }
}

/* Kernel main function */
void kernel_main(void) {
    uint8_t color = (VGA_COLOR_BLACK << 4) | VGA_COLOR_WHITE;

    /* Clear screen */
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        uint16_t *vga = (uint16_t *)VGA_BUFFER;
        vga[i] = (uint16_t)((color << 8) | ' ');
    }

    /* Initialize serial port */
    serial_init();

    /* Print to VGA */
    vga_puts("Hello, JAKernel!", 0, 0, color);

    /* Print to serial */
    serial_puts("Hello, JAKernel!\n");

    /* Halt */
    while (1) {
        asm volatile ("hlt");
    }
}
