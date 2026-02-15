/**
 * lib/minilibc/printf.c - Minimal formatted output implementation
 *
 * Supports essential format specifiers for kernel use:
 *   %d, %i - signed decimal integer
 *   %u     - unsigned decimal integer
 *   %x, %X - hexadecimal (lowercase/uppercase)
 *   %p     - pointer (as hex with 0x prefix)
 *   %s     - null-terminated string
 *   %c     - single character
 *   %%     - literal percent sign
 */

#include <string.h>
#include <stdarg.h>

/* ==== Internal Helpers ==== */

/**
 * emit_char - Write a single character to buffer
 * Returns 1 on success, 0 if buffer full
 */
static int emit_char(char **buf, size_t *remaining, char c) {
    if (*remaining > 1) {
        *(*buf)++ = c;
        (*remaining)--;
        return 1;
    }
    return 0;
}

/**
 * emit_string - Write a string to buffer
 * Returns number of characters written
 */
static size_t emit_string(char **buf, size_t *remaining, const char *s) {
    size_t written = 0;
    while (*s) {
        if (!emit_char(buf, remaining, *s)) {
            /* Continue counting for return value */
        }
        written++;
        s++;
    }
    return written;
}

/**
 * emit_uint - Write unsigned integer to buffer
 * @base: 10 for decimal, 16 for hex
 * @uppercase: Use uppercase letters for hex (A-F vs a-f)
 *
 * Returns number of characters written (not truncated)
 */
static size_t emit_uint(char **buf, size_t *remaining, unsigned long val, int base, int uppercase) {
    char tmp[32];  /* Enough for 64-bit in any base */
    int i = 0;
    const char digits_lower[] = "0123456789abcdef";
    const char digits_upper[] = "0123456789ABCDEF";
    const char *digits = uppercase ? digits_upper : digits_lower;

    /* Special case: zero */
    if (val == 0) {
        if (*remaining > 1) {
            *(*buf)++ = '0';
            (*remaining)--;
        }
        return 1;
    }

    /* Build digits in reverse */
    while (val > 0) {
        tmp[i++] = digits[val % base];
        val /= base;
    }

    /* Output in correct order */
    size_t written = i;
    while (i > 0) {
        if (*remaining > 1) {
            *(*buf)++ = tmp[--i];
            (*remaining)--;
        } else {
            i--;
        }
    }

    return written;
}

/**
 * emit_int - Write signed integer to buffer
 * Returns number of characters written (not truncated)
 */
static size_t emit_int(char **buf, size_t *remaining, long val) {
    size_t written = 0;

    if (val < 0) {
        if (*remaining > 1) {
            *(*buf)++ = '-';
            (*remaining)--;
        }
        written++;
        /* Handle INT_MIN carefully - negate as unsigned */
        val = -(unsigned long)val;
    }

    written += emit_uint(buf, remaining, (unsigned long)val, 10, 0);
    return written;
}

/* ==== Public API ==== */

int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    char *buf = str;
    size_t remaining = size;
    size_t written = 0;

    if (size == 0) {
        /* Special case: just count what would be written */
        buf = NULL;
        remaining = 0;
    }

    const char *p = format;
    while (*p) {
        if (*p != '%') {
            if (remaining > 1) {
                *buf++ = *p;
                remaining--;
            }
            written++;
            p++;
            continue;
        }

        /* Handle format specifier */
        p++;  /* Skip '%' */

        /* TODO: Add width/precision support here if needed:
         *       - Parse field width (e.g., %10d)
         *       - Parse precision (e.g., %.5s)
         *       These modifiers would be parsed before the format char
         */

        switch (*p) {
            case '%':
                /* Literal percent */
                if (remaining > 1) {
                    *buf++ = '%';
                    remaining--;
                }
                written++;
                break;

            case 'c':
                /* Character */
                {
                    char c = (char)va_arg(ap, int);
                    if (remaining > 1) {
                        *buf++ = c;
                        remaining--;
                    }
                    written++;
                }
                break;

            case 's':
                /* String */
                {
                    const char *s = va_arg(ap, const char *);
                    if (s == NULL) {
                        s = "(null)";
                    }
                    written += emit_string(&buf, &remaining, s);
                }
                break;

            case 'd':
            case 'i':
                /* Signed decimal integer */
                written += emit_int(&buf, &remaining, va_arg(ap, int));
                break;

            case 'u':
                /* Unsigned decimal integer */
                written += emit_uint(&buf, &remaining, va_arg(ap, unsigned int), 10, 0);
                break;

            case 'x':
                /* Hex lowercase */
                written += emit_uint(&buf, &remaining, va_arg(ap, unsigned int), 16, 0);
                break;

            case 'X':
                /* Hex uppercase */
                written += emit_uint(&buf, &remaining, va_arg(ap, unsigned int), 16, 1);
                break;

            case 'p':
                /* Pointer: 0x prefix + hex */
                {
                    void *ptr = va_arg(ap, void *);
                    if (remaining > 1) {
                        *buf++ = '0';
                        remaining--;
                    }
                    if (remaining > 1) {
                        *buf++ = 'x';
                        remaining--;
                    }
                    written += 2;
                    written += emit_uint(&buf, &remaining, (unsigned long)ptr, 16, 0);
                }
                break;

            case 'l':
                /* Long modifier - peek at next char */
                p++;
                if (*p == 'd' || *p == 'i') {
                    written += emit_int(&buf, &remaining, va_arg(ap, long));
                } else if (*p == 'u') {
                    written += emit_uint(&buf, &remaining, va_arg(ap, unsigned long), 10, 0);
                } else if (*p == 'x') {
                    written += emit_uint(&buf, &remaining, va_arg(ap, unsigned long), 16, 0);
                } else if (*p == 'X') {
                    written += emit_uint(&buf, &remaining, va_arg(ap, unsigned long), 16, 1);
                } else {
                    /* Unknown %l? format - print literal */
                    if (remaining > 1) {
                        *buf++ = '%';
                        remaining--;
                    }
                    written++;
                    if (remaining > 1) {
                        *buf++ = 'l';
                        remaining--;
                    }
                    written++;
                    if (*p) {
                        if (remaining > 1) {
                            *buf++ = *p;
                            remaining--;
                        }
                        written++;
                    }
                }
                break;

            case '\0':
                /* Premature end of format string */
                p--;
                break;

            default:
                /* Unknown specifier - print literally */
                if (remaining > 1) {
                    *buf++ = '%';
                    remaining--;
                }
                written++;
                if (remaining > 1) {
                    *buf++ = *p;
                    remaining--;
                }
                written++;
                break;
        }

        if (*p) {
            p++;
        }
    }

    /* Null-terminate */
    if (size > 0) {
        *buf = '\0';
    }

    return (int)written;
}

int snprintf(char *str, size_t size, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = vsnprintf(str, size, format, ap);
    va_end(ap);
    return ret;
}
