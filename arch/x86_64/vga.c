/* Emergence Kernel - x86-64 VGA text mode output */

#include <stdint.h>
#include "arch/x86_64/vga.h"

/* VGA text mode buffer at physical address 0xB8000 */
#define VGA_BUFFER ((volatile uint16_t *)0xB8000)

/* Initialize VGA text mode by clearing the screen */
void vga_init(void) {
    uint8_t color = VGA_COLOR(VGA_COLOR_BLACK, VGA_COLOR_WHITE);

    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        VGA_BUFFER[i] = (uint16_t)((color << 8) | ' ');
    }
}

/* Write a single character at specified position */
void vga_putc(char c, int row, int col, uint8_t color) {
    VGA_BUFFER[row * VGA_WIDTH + col] = (uint16_t)((color << 8) | c);
}

/* Write a string starting at specified position */
void vga_puts(const char *str, int row, int col, uint8_t color) {
    while (*str) {
        vga_putc(*str, row, col, color);
        col++;
        str++;
    }
}
