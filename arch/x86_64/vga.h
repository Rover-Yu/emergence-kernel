/* Emergence Kernel - x86-64 VGA text mode interface */

#ifndef EMERGENCE_ARCH_X86_64_VGA_H
#define EMERGENCE_ARCH_X86_64_VGA_H

#include <stdint.h>

/* VGA text mode dimensions */
#define VGA_WIDTH   80
#define VGA_HEIGHT  25

/* VGA colors */
#define VGA_COLOR_BLACK     0
#define VGA_COLOR_WHITE     15

/* Combine foreground and background colors */
#define VGA_COLOR(fg, bg)   (((bg) << 4) | (fg))

/* VGA functions */
void vga_init(void);
void vga_putc(char c, int row, int col, uint8_t color);
void vga_puts(const char *str, int row, int col, uint8_t color);

#endif /* EMERGENCE_ARCH_X86_64_VGA_H */
