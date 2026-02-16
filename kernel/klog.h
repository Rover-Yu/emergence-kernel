/* Emergence Kernel - Kernel Logging Subsystem (klog)
 *
 * Provides structured logging with levels (DEBUG, INFO, WARN, ERROR).
 * Default level is INFO (DEBUG messages hidden by default).
 * Runtime control via kernel command line: log=debug/info/warn/error
 *
 * Message format: [CPU0][INFO][SUBSYS] message
 */

#ifndef _KERNEL_KLOG_H
#define _KERNEL_KLOG_H

#include <stdint.h>
#include <stdarg.h>

/* Log levels */
#define KLOG_DEBUG  0
#define KLOG_INFO   1
#define KLOG_WARN   2
#define KLOG_ERROR  3

/* Default level */
#define KLOG_DEFAULT_LEVEL KLOG_INFO

/* Public API - variadic logging functions */
void klog_debug(const char *subsys, const char *fmt, ...);
void klog_info(const char *subsys, const char *fmt, ...);
void klog_warn(const char *subsys, const char *fmt, ...);
void klog_error(const char *subsys, const char *fmt, ...);

/* Configuration */
void klog_init(void);
void klog_set_level(int level);
int klog_get_level(void);

#endif /* _KERNEL_KLOG_H */
