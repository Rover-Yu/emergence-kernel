/* Syscall Test Header
 *
 * Test framework for testing new syscalls
 */

#ifndef _TEST_SYSCALL_H
#define _TEST_SYSCALL_H

#include <stdint.h>

/* Run all syscall tests */
int run_syscall_tests(void);

/* Test wrapper for kernel test framework */
void test_syscall(void);

/* Syscall test preparation - returns 1 if test is selected */
int test_syscall_prepare(void);

#endif /* _TEST_SYSCALL_H */
