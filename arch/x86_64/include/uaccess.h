/* Emergence Kernel - User Space Access Utilities
 *
 * This header provides safe methods for accessing user memory from kernel space.
 * All functions include proper validation and error handling to prevent kernel
 * crashes or security issues when dealing with user-provided pointers.
 */

#ifndef _UACCESS_H
#define _UACCESS_H

#include <stdint.h>
#include <stddef.h>

/* User pointer annotation - marks pointers as user space addresses
 * Note: GCC's address_space attribute causes warnings, so we use a simple marker */
#define __user

/* Error codes for user access operations */
#define EFAULT  -14      /* Bad address */
#define ENOMEM  -12      /* Out of memory */
#define EINVAL  -22      /* Invalid argument */
#define ENOENT  -2       /* No such file or directory */

/* User memory region bounds
 * TODO: Make these dynamic per-process when full VM is implemented
 */
#define USER_CODE_BASE    0x400000      /* 4MB - typical user code start */
#define USER_CODE_END     0x7fffffffffff /* User space limit (non-canonical check) */

/**
 * copy_from_user - Copy data from user space to kernel space
 * @dst: Destination kernel buffer
 * @src: Source user buffer
 * @n: Number of bytes to copy
 *
 * Returns: Number of bytes NOT copied (0 = success), or negative error code
 *
 * This function validates that the user pointer is within valid user space
 * range before performing the copy. If validation fails, returns -EFAULT.
 *
 * Usage:
 *   long err = copy_from_user(kernel_buf, user_ptr, size);
 *   if (err) {
 *       // Handle error - user_ptr was invalid
 *   }
 */
long copy_from_user(void *dst, const void __user *src, size_t n);

/**
 * copy_to_user - Copy data from kernel space to user space
 * @dst: Destination user buffer
 * @src: Source kernel buffer
 * @n: Number of bytes to copy
 *
 * Returns: Number of bytes NOT copied (0 = success), or negative error code
 *
 * This function validates that the user pointer is within valid user space
 * range before performing the copy.
 *
 * Usage:
 *   long err = copy_to_user(user_ptr, kernel_buf, size);
 *   if (err) {
 *       // Handle error - user_ptr was invalid
 *   }
 */
long copy_to_user(void __user *dst, const void *src, size_t n);

/**
 * strncpy_from_user - Copy null-terminated string from user space
 * @dst: Destination kernel buffer
 * @src: Source user string
 * @count: Maximum bytes to copy (including null terminator)
 *
 * Returns: Length of string (excluding null) on success, or negative error code
 *
 * Safely copies a string from user space, ensuring the destination is
 * always null-terminated. Returns -EFAULT if user pointer is invalid.
 */
long strncpy_from_user(char *dst, const char __user *src, size_t count);

/**
 * probe_user_read - Check if user address range is readable
 * @addr: Start of user address range
 * @n: Length of range in bytes
 *
 * Returns: 0 if readable, -EFAULT if not readable
 *
 * Validates that the address range falls within user space bounds.
 * Does NOT perform actual memory access - just checks address ranges.
 *
 * Note: This is a basic implementation. A full version would walk page tables
 * to verify each page is mapped and readable.
 */
int probe_user_read(const void __user *addr, size_t n);

/**
 * probe_user_write - Check if user address range is writable
 * @addr: Start of user address range
 * @n: Length of range in bytes
 *
 * Returns: 0 if writable, -EFAULT if not writable
 *
 * Similar to probe_user_read but validates write permissions.
 */
int probe_user_write(void __user *addr, size_t n);

/**
 * clear_user - Clear a user memory region
 * @dst: User buffer to clear
 * @n: Number of bytes to clear
 *
 * Returns: Number of bytes NOT cleared (0 = success), or negative error code
 *
 * Zeros out the specified user memory region after validating the pointer.
 */
long clear_user(void __user *dst, size_t n);

/**
 * user_access_begin - Begin a user access region
 *
 * Disables page faults during user access. Must be paired with
 * user_access_end(). Used for批量用户访问优化.
 *
 * Note: Current implementation is a stub. Full implementation would
 * use exception tables for efficient fault handling.
 */
static inline void user_access_begin(void)
{
    /* Future: Enable exception table mechanism */
}

/**
 * user_access_end - End a user access region
 *
 * Re-enables page faults after user access is complete.
 */
static inline void user_access_end(void)
{
    /* Future: Disable exception table mechanism */
}

/**
 * unsafe_get_user - Get a value from user address (without validation)
 * @x: Variable to store result
 * @ptr: User pointer to read from
 *
 * Returns: 0 on success, -EFAULT on fault
 *
 * MUST be used between user_access_begin() and user_access_end().
 * This is an optimized version that skips validation checks.
 */
#define unsafe_get_user(x, ptr) ({                  \
    long __ret = 0;                                 \
    typeof(ptr) __ptr = (ptr);                      \
    typeof(x) __val;                                \
    if (probe_user_read(__ptr, sizeof(__val))) {    \
        __ret = -EFAULT;                            \
    } else {                                        \
        __val = *(typeof(__val) *)__ptr;            \
        (x) = __val;                                \
    }                                               \
    __ret;                                          \
})

/**
 * unsafe_put_user - Put a value to user address (without validation)
 * @x: Value to store
 * @ptr: User pointer to write to
 *
 * Returns: 0 on success, -EFAULT on fault
 *
 * MUST be used between user_access_begin() and user_access_end().
 */
#define unsafe_put_user(x, ptr) ({                  \
    long __ret = 0;                                 \
    typeof(ptr) __ptr = (ptr);                      \
    if (probe_user_write(__ptr, sizeof(x))) {       \
        __ret = -EFAULT;                            \
    } else {                                        \
        *(typeof(x) *)__ptr = (x);                  \
    }                                               \
    __ret;                                          \
})

#endif /* _UACCESS_H */
