/* Emergence Kernel - Kernel Logging Subsystem (klog) Implementation
 *
 * Provides structured logging with levels and runtime control.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include "kernel/klog.h"
#include "arch/x86_64/serial.h"
#include "arch/x86_64/multiboot2.h"
#include "arch/x86_64/smp.h"

/* External minilibc function */
extern int vsnprintf(char *str, size_t size, const char *format, va_list ap);

/* Current log level - default to INFO */
static int current_level = KLOG_DEFAULT_LEVEL;

/* Level name strings */
static const char *level_names[] = {
    "DEBUG", "INFO", "WARN", "ERROR"
};

/* Maximum message buffer size */
#define KLOG_BUF_SIZE 512

/**
 * klog_init - Initialize the logging subsystem
 *
 * Parses the kernel command line for log= parameter.
 * Must be called after cmdline is available.
 */
void klog_init(void) {
    const char *level_str = cmdline_get_value("log");

    if (level_str == NULL) {
        /* No log= parameter, use default */
        return;
    }

    /* Parse log level */
    if (level_str[0] == 'd' || level_str[0] == 'D') {
        current_level = KLOG_DEBUG;
    } else if (level_str[0] == 'i' || level_str[0] == 'I') {
        current_level = KLOG_INFO;
    } else if (level_str[0] == 'w' || level_str[0] == 'W') {
        current_level = KLOG_WARN;
    } else if (level_str[0] == 'e' || level_str[0] == 'E') {
        current_level = KLOG_ERROR;
    }
    /* Invalid values keep default */
}

/**
 * klog_set_level - Set the current log level
 * @level: New log level (KLOG_DEBUG, KLOG_INFO, KLOG_WARN, KLOG_ERROR)
 */
void klog_set_level(int level) {
    if (level >= KLOG_DEBUG && level <= KLOG_ERROR) {
        current_level = level;
    }
}

/**
 * klog_get_level - Get the current log level
 *
 * Returns: Current log level
 */
int klog_get_level(void) {
    return current_level;
}

/**
 * klog_vprintf - Internal logging function
 * @level: Log level of this message
 * @subsys: Subsystem tag (e.g., "PMM", "SLAB")
 * @fmt: Printf-style format string
 * @ap: Variadic arguments
 */
static void klog_vprintf(int level, const char *subsys, const char *fmt, va_list ap) {
    char buf[KLOG_BUF_SIZE];
    int pos = 0;
    int cpu_id;

    /* Filter by level */
    if (level < current_level) {
        return;
    }

    /* Get CPU ID */
    cpu_id = smp_get_cpu_index();

    /* Format: [CPU0][INFO][SUBSYS] message\n */
    buf[pos++] = '[';
    buf[pos++] = 'C';
    buf[pos++] = 'P';
    buf[pos++] = 'U';
    buf[pos++] = '0' + cpu_id;
    buf[pos++] = ']';
    buf[pos++] = '[';

    /* Level name */
    const char *lname = level_names[level];
    while (*lname && pos < KLOG_BUF_SIZE - 20) {
        buf[pos++] = *lname++;
    }

    buf[pos++] = ']';
    buf[pos++] = '[';

    /* Subsystem tag */
    while (*subsys && pos < KLOG_BUF_SIZE - 10) {
        buf[pos++] = *subsys++;
    }

    buf[pos++] = ']';
    buf[pos++] = ' ';

    /* Null-terminate prefix */
    buf[pos] = '\0';

    /* Output prefix */
    serial_puts(buf);

    /* Format and output the message */
    vsnprintf(buf, KLOG_BUF_SIZE, fmt, ap);
    serial_puts(buf);

    /* Add newline if not present */
    /* Find end of formatted string and check last char */
    char *end = buf;
    while (*end) end++;
    if (end == buf || *(end - 1) != '\n') {
        serial_puts("\n");
    }
}

/**
 * klog_debug - Log a debug message
 * @subsys: Subsystem tag
 * @fmt: Printf-style format string
 */
void klog_debug(const char *subsys, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    klog_vprintf(KLOG_DEBUG, subsys, fmt, ap);
    va_end(ap);
}

/**
 * klog_info - Log an info message
 * @subsys: Subsystem tag
 * @fmt: Printf-style format string
 */
void klog_info(const char *subsys, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    klog_vprintf(KLOG_INFO, subsys, fmt, ap);
    va_end(ap);
}

/**
 * klog_warn - Log a warning message
 * @subsys: Subsystem tag
 * @fmt: Printf-style format string
 */
void klog_warn(const char *subsys, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    klog_vprintf(KLOG_WARN, subsys, fmt, ap);
    va_end(ap);
}

/**
 * klog_error - Log an error message
 * @subsys: Subsystem tag
 * @fmt: Printf-style format string
 */
void klog_error(const char *subsys, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    klog_vprintf(KLOG_ERROR, subsys, fmt, ap);
    va_end(ap);
}
