/**
 * include/string.h - Minimal string library for Emergence Kernel
 *
 * Provides essential string manipulation functions without external libc.
 * All functions are self-contained and use only stack memory.
 */

#ifndef MINILIBC_STRING_H
#define MINILIBC_STRING_H

#include <stdint.h>

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

#endif /* MINILIBC_STRING_H */
