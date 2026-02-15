/**
 * include/string.h - Minimal string library for Emergence Kernel
 *
 * Provides essential string manipulation functions without external libc.
 * All functions are self-contained and use only stack memory.
 */

#ifndef MINILIBC_STRING_H
#define MINILIBC_STRING_H

#include <stdint.h>
#include <stdarg.h>

/* ==== String Length ==== */

/**
 * strlen - Calculate string length
 * @s: Null-terminated string
 *
 * Returns: Length of string (excluding null terminator)
 */
size_t strlen(const char *s);

/* ==== String Copy ==== */

/**
 * strcpy - Copy string
 * @dest: Destination buffer
 * @src: Source string
 *
 * Returns: Pointer to destination
 *
 * NOTE: Caller must ensure dest has enough space
 */
char *strcpy(char *dest, const char *src);

/* ==== String Comparison ==== */

/**
 * strcmp - Compare two strings
 * @s1: First string
 * @s2: Second string
 *
 * Returns: 0 if equal, negative if s1 < s2, positive if s1 > s2
 */
int strcmp(const char *s1, const char *s2);

/**
 * strncmp - Compare two strings with limit
 * @s1: First string
 * @s2: Second string
 * @n: Maximum characters to compare
 *
 * Returns: 0 if equal, negative if s1 < s2, positive if s1 > s2
 */
int strncmp(const char *s1, const char *s2, size_t n);

/* ==== Memory Operations ==== */

/**
 * memset - Fill memory with constant byte
 * @s: Memory area
 * @c: Byte value to fill
 * @n: Number of bytes
 *
 * Returns: Pointer to s
 */
void *memset(void *s, int c, size_t n);

/**
 * memcpy - Copy memory area
 * @dest: Destination
 * @src: Source
 * @n: Number of bytes
 *
 * Returns: Pointer to dest
 *
 * NOTE: Caller must ensure areas do not overlap
 */
void *memcpy(void *dest, const void *src, size_t n);

/* ==== Formatted Output ==== */

/**
 * snprintf - Format string to buffer with size limit
 * @str: Destination buffer
 * @size: Buffer size (including space for null terminator)
 * @format: Format string
 * @...: Variable arguments
 *
 * Supports: %d, %i, %u, %x, %X, %p, %s, %c, %%
 *
 * Returns: Number of characters that would have been written (excluding null)
 *          if buffer was large enough. Negative on error.
 *
 * NOTE: Output is always null-terminated if size > 0
 */
int snprintf(char *str, size_t size, const char *format, ...);

/**
 * vsnprintf - Format string to buffer with va_list
 * @str: Destination buffer
 * @size: Buffer size (including space for null terminator)
 * @format: Format string
 * @ap: Variable argument list
 *
 * Returns: Number of characters that would have been written (excluding null)
 */
int vsnprintf(char *str, size_t size, const char *format, va_list ap);

#endif /* MINILIBC_STRING_H */
