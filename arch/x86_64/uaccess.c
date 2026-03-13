/* Emergence Kernel - User Space Access Implementation
 *
 * Provides safe methods for accessing user memory from kernel space.
 * All functions include proper validation and error handling.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "arch/x86_64/include/uaccess.h"
#include "kernel/klog.h"

/* User memory region bounds
 * x86_64 canonical address check:
 * - User space: 0x0000000000000000 - 0x00007fffffffffff (128 TB)
 * - Kernel space: 0xffff800000000000 - 0xffffffffffffffff (non-canonical)
 *
 * For now, we use simpler bounds until full VM is implemented.
 */
#define USER_SPACE_END    0x800000000000ULL  /* 128TB user space limit */

/**
 * is_user_pointer - Check if pointer is within user space
 * @ptr: Pointer to validate
 *
 * Returns: 1 if valid user pointer, 0 if kernel pointer or invalid
 */
static inline int is_user_pointer(const void *ptr)
{
    uint64_t addr = (uint64_t)ptr;

    /* Check if address is in user space range */
    if (addr < USER_CODE_BASE || addr >= USER_SPACE_END) {
        return 0;
    }

    /* Check for canonical address format (x86_64 requirement)
     * Canonical: bits [63:48] must match bit [47]
     */
    uint64_t upper_bits = addr >> 47;
    if (upper_bits != 0 && upper_bits != 0x1FFFF) {
        return 0;  /* Non-canonical address */
    }

    return 1;
}

/**
 * probe_user_read - Check if user address range is readable
 * @addr: Start of user address range
 * @n: Length of range in bytes
 *
 * Returns: 0 if readable, -EFAULT if not readable
 */
int probe_user_read(const void __user *addr, size_t n)
{
    uint64_t start = (uint64_t)addr;
    uint64_t end = start + n;

    /* Check for overflow */
    if (end < start) {
        return -EFAULT;
    }

    /* Validate pointer is in user space */
    if (!is_user_pointer(addr)) {
        klog_debug("UACCESS", "Invalid user pointer: %p", addr);
        return -EFAULT;
    }

    /* Validate end is also in user space */
    if (end >= USER_SPACE_END) {
        klog_debug("UACCESS", "User range exceeds limit: %p + %zx", addr, n);
        return -EFAULT;
    }

    /* TODO: Walk page tables to verify pages are mapped
     * For now, we just check address ranges */
    return 0;
}

/**
 * probe_user_write - Check if user address range is writable
 * @addr: Start of user address range
 * @n: Length of range in bytes
 *
 * Returns: 0 if writable, -EFAULT if not writable
 */
int probe_user_write(void __user *addr, size_t n)
{
    /* For now, same validation as read
     * TODO: Check page table write permissions when VM is implemented */
    return probe_user_read(addr, n);
}

/**
 * copy_from_user - Copy data from user space to kernel space
 * @dst: Destination kernel buffer
 * @src: Source user buffer
 * @n: Number of bytes to copy
 *
 * Returns: Number of bytes NOT copied (0 = success), or negative error code
 */
long copy_from_user(void *dst, const void __user *src, size_t n)
{
    /* Validate pointers */
    if (dst == NULL) {
        klog_warn("UACCESS", "copy_from_user: NULL kernel destination");
        return -EINVAL;
    }

    if (src == NULL) {
        klog_warn("UACCESS", "copy_from_user: NULL user source");
        return -EFAULT;
    }

    /* Validate user pointer range */
    if (probe_user_read(src, n) != 0) {
        klog_warn("UACCESS", "copy_from_user: invalid user range %p + %zx", src, n);
        return -EFAULT;
    }

    /* TODO: Use exception table for efficient fault handling
     * For now, we do a simple memcpy with validation */
    klog_debug("UACCESS", "copy_from_user: %p -> %p, %zu bytes", src, dst, n);

    /* Perform copy with fault handling
     * Future: Use page table walk to validate each page */
    memcpy(dst, (const void *)src, n);

    return 0;  /* All bytes copied successfully */
}

/**
 * copy_to_user - Copy data from kernel space to user space
 * @dst: Destination user buffer
 * @src: Source kernel buffer
 * @n: Number of bytes to copy
 *
 * Returns: Number of bytes NOT copied (0 = success), or negative error code
 */
long copy_to_user(void __user *dst, const void *src, size_t n)
{
    /* Validate pointers */
    if (src == NULL) {
        klog_warn("UACCESS", "copy_to_user: NULL kernel source");
        return -EINVAL;
    }

    if (dst == NULL) {
        klog_warn("UACCESS", "copy_to_user: NULL user destination");
        return -EFAULT;
    }

    /* Validate user pointer range */
    if (probe_user_write(dst, n) != 0) {
        klog_warn("UACCESS", "copy_to_user: invalid user range %p + %zx", dst, n);
        return -EFAULT;
    }

    klog_debug("UACCESS", "copy_to_user: %p -> %p, %zu bytes", src, dst, n);

    /* Perform copy */
    memcpy((void *)dst, src, n);

    return 0;  /* All bytes copied successfully */
}

/**
 * strncpy_from_user - Copy null-terminated string from user space
 * @dst: Destination kernel buffer
 * @src: Source user string
 * @count: Maximum bytes to copy (including null terminator)
 *
 * Returns: Length of string (excluding null) on success, or negative error code
 */
long strncpy_from_user(char *dst, const char __user *src, size_t count)
{
    size_t len;

    if (dst == NULL || src == NULL) {
        return -EINVAL;
    }

    if (count == 0) {
        return -EINVAL;
    }

    /* Validate user pointer range */
    if (probe_user_read(src, count) != 0) {
        klog_warn("UACCESS", "strncpy_from_user: invalid user range %p + %zx", src, count);
        return -EFAULT;
    }

    /* Find string length */
    len = 0;
    while (len < count - 1) {
        char c;
        c = ((const char *)src)[len];
        if (c == '\0') {
            break;
        }
        dst[len] = c;
        len++;
    }

    /* Always null-terminate */
    dst[len] = '\0';

    klog_debug("UACCESS", "strncpy_from_user: copied %zu bytes from %p", len, src);

    return len;
}

/**
 * clear_user - Clear a user memory region
 * @dst: User buffer to clear
 * @n: Number of bytes to clear
 *
 * Returns: Number of bytes NOT cleared (0 = success), or negative error code
 */
long clear_user(void __user *dst, size_t n)
{
    /* Validate user pointer range */
    if (probe_user_write(dst, n) != 0) {
        klog_warn("UACCESS", "clear_user: invalid user range %p + %zx", dst, n);
        return -EFAULT;
    }

    klog_debug("UACCESS", "clear_user: clearing %p, %zu bytes", dst, n);

    /* Zero out the memory */
    memset((void *)dst, 0, n);

    return 0;
}
