/* Emergence Kernel - Test Framework Implementation */

#include <stdint.h>
#include <stddef.h>
#include "kernel/test.h"
#include "arch/x86_64/multiboot2.h"
#include "arch/x86_64/serial.h"
#include "arch/x86_64/power.h"

/* External cmdline_get_value from multiboot2.c */
extern const char *cmdline_get_value(const char *key);

/* Test selection state */
static enum {
    TEST_MODE_NONE,      /* No test= parameter (default) */
    TEST_MODE_ALL,       /* test=all: Run all auto_run tests */
    TEST_MODE_SPECIFIC,  /* test=<name>: Run specific test */
    TEST_MODE_UNIFIED    /* test=unified: Run all selected at end */
} test_mode = TEST_MODE_NONE;

/* Specific test name (for TEST_MODE_SPECIFIC) */
static const char *selected_test_name = NULL;

/* Track which tests have already run (for unified mode) */
#define MAX_TESTS 16
static char tests_run[MAX_TESTS];  /* 0 = not run, 1 = run */
static int tests_run_count = 0;

/* ============================================================================
 * Test Registry
 * ============================================================================ */

#include "include/string.h"

#if CONFIG_TESTS_MINILIBC
/* Minilibc string library tests */

/* ============================================================================
 * strlen Tests
 * ============================================================================ */

static int test_strlen_empty(void) {
    size_t len = strlen("");
    if (len != 0) {
        serial_puts("[MINILIBC test] strlen(\"\") FAILED: expected 0\n");
        return -1;
    }
    serial_puts("[MINILIBC test] strlen(\"\") PASSED\n");
    return 0;
}

static int test_strlen_basic(void) {
    size_t len = strlen("hello");
    if (len != 5) {
        serial_puts("[MINILIBC test] strlen(\"hello\") FAILED: expected 5\n");
        return -1;
    }
    serial_puts("[MINILIBC test] strlen(\"hello\") PASSED\n");
    return 0;
}

static int test_strlen_single_char(void) {
    size_t len = strlen("A");
    if (len != 1) {
        serial_puts("[MINILIBC test] strlen(\"A\") FAILED: expected 1\n");
        return -1;
    }
    serial_puts("[MINILIBC test] strlen(\"A\") PASSED\n");
    return 0;
}

static int test_strlen_with_spaces(void) {
    size_t len = strlen("hello world");
    if (len != 11) {
        serial_puts("[MINILIBC test] strlen(\"hello world\") FAILED: expected 11\n");
        return -1;
    }
    serial_puts("[MINILIBC test] strlen(\"hello world\") PASSED\n");
    return 0;
}

static int test_strlen_special_chars(void) {
    size_t len = strlen("\n\t\x01");
    if (len != 3) {
        serial_puts("[MINILIBC test] strlen(special chars) FAILED: expected 3\n");
        return -1;
    }
    serial_puts("[MINILIBC test] strlen(special chars) PASSED\n");
    return 0;
}

/* ============================================================================
 * strcpy Tests
 * ============================================================================ */

static int test_strcpy_basic(void) {
    char dest[20];
    const char *src = "hello";
    char *result = strcpy(dest, src);

    if (result != dest) {
        serial_puts("[MINILIBC test] strcpy FAILED: wrong return value\n");
        return -1;
    }
    if (strcmp(dest, src) != 0) {
        serial_puts("[MINILIBC test] strcpy FAILED: copy mismatch\n");
        return -1;
    }
    serial_puts("[MINILIBC test] strcpy(basic) PASSED\n");
    return 0;
}

static int test_strcpy_empty_src(void) {
    char dest[20] = "garbage";
    char *result = strcpy(dest, "");

    if (result != dest) {
        serial_puts("[MINILIBC test] strcpy(empty) FAILED: wrong return value\n");
        return -1;
    }
    if (dest[0] != '\0') {
        serial_puts("[MINILIBC test] strcpy(empty) FAILED: not null terminated\n");
        return -1;
    }
    serial_puts("[MINILIBC test] strcpy(empty) PASSED\n");
    return 0;
}

static int test_strcpy_single_char(void) {
    char dest[20];
    char *result = strcpy(dest, "X");

    if (result != dest) {
        serial_puts("[MINILIBC test] strcpy(single) FAILED: wrong return value\n");
        return -1;
    }
    if (dest[0] != 'X' || dest[1] != '\0') {
        serial_puts("[MINILIBC test] strcpy(single) FAILED: incorrect copy\n");
        return -1;
    }
    serial_puts("[MINILIBC test] strcpy(single) PASSED\n");
    return 0;
}

static int test_strcopy_with_spaces(void) {
    char dest[20];
    strcpy(dest, "hello world");

    if (strcmp(dest, "hello world") != 0) {
        serial_puts("[MINILIBC test] strcpy(spaces) FAILED: copy mismatch\n");
        return -1;
    }
    serial_puts("[MINILIBC test] strcpy(spaces) PASSED\n");
    return 0;
}

/* ============================================================================
 * strcmp Tests
 * ============================================================================ */

static int test_strcmp_equal(void) {
    if (strcmp("hello", "hello") != 0) {
        serial_puts("[MINILIBC test] strcmp(equal) FAILED\n");
        return -1;
    }
    serial_puts("[MINILIBC test] strcmp(equal) PASSED\n");
    return 0;
}

static int test_strcmp_both_empty(void) {
    if (strcmp("", "") != 0) {
        serial_puts("[MINILIBC test] strcmp(both empty) FAILED\n");
        return -1;
    }
    serial_puts("[MINILIBC test] strcmp(both empty) PASSED\n");
    return 0;
}

static int test_strcmp_one_empty(void) {
    if (strcmp("", "abc") >= 0) {
        serial_puts("[MINILIBC test] strcmp(empty < nonempty) FAILED\n");
        return -1;
    }
    if (strcmp("abc", "") <= 0) {
        serial_puts("[MINILIBC test] strcmp(nonempty > empty) FAILED\n");
        return -1;
    }
    serial_puts("[MINILIBC test] strcmp(one empty) PASSED\n");
    return 0;
}

static int test_strcmp_less(void) {
    if (strcmp("abc", "def") >= 0) {
        serial_puts("[MINILIBC test] strcmp(less) FAILED\n");
        return -1;
    }
    serial_puts("[MINILIBC test] strcmp(less) PASSED\n");
    return 0;
}

static int test_strcmp_greater(void) {
    if (strcmp("xyz", "abc") <= 0) {
        serial_puts("[MINILIBC test] strcmp(greater) FAILED\n");
        return -1;
    }
    serial_puts("[MINILIBC test] strcmp(greater) PASSED\n");
    return 0;
}

static int test_strcmp_prefix(void) {
    if (strcmp("abc", "abcd") >= 0) {
        serial_puts("[MINILIBC test] strcmp(prefix) FAILED\n");
        return -1;
    }
    serial_puts("[MINILIBC test] strcmp(prefix) PASSED\n");
    return 0;
}

static int test_strcmp_case_sensitive(void) {
    if (strcmp("ABC", "abc") == 0) {
        serial_puts("[MINILIBC test] strcmp(case) FAILED: should be case sensitive\n");
        return -1;
    }
    serial_puts("[MINILIBC test] strcmp(case) PASSED\n");
    return 0;
}

/* ============================================================================
 * strncmp Tests
 * ============================================================================ */

static int test_strncmp_equal(void) {
    if (strncmp("hello", "hello", 10) != 0) {
        serial_puts("[MINILIBC test] strncmp(equal) FAILED\n");
        return -1;
    }
    serial_puts("[MINILIBC test] strncmp(equal) PASSED\n");
    return 0;
}

static int test_strncmp_equal_prefix(void) {
    if (strncmp("hello", "helium", 3) != 0) {
        serial_puts("[MINILIBC test] strncmp(equal prefix) FAILED\n");
        return -1;
    }
    serial_puts("[MINILIBC test] strncmp(equal prefix) PASSED\n");
    return 0;
}

static int test_strncmp_zero(void) {
    if (strncmp("abc", "xyz", 0) != 0) {
        serial_puts("[MINILIBC test] strncmp(zero limit) FAILED\n");
        return -1;
    }
    serial_puts("[MINILIBC test] strncmp(zero limit) PASSED\n");
    return 0;
}

static int test_strncmp_both_empty(void) {
    if (strncmp("", "", 10) != 0) {
        serial_puts("[MINILIBC test] strncmp(both empty) FAILED\n");
        return -1;
    }
    serial_puts("[MINILIBC test] strncmp(both empty) PASSED\n");
    return 0;
}

static int test_strncmp_limit_shorter(void) {
    if (strncmp("abc", "xyz", 1) == 0) {
        serial_puts("[MINILIBC test] strncmp(limit shorter) FAILED\n");
        return -1;
    }
    serial_puts("[MINILIBC test] strncmp(limit shorter) PASSED\n");
    return 0;
}

static int test_strncmp_one_is_prefix(void) {
    if (strncmp("abc", "abcd", 4) >= 0) {
        serial_puts("[MINILIBC test] strncmp(prefix) FAILED\n");
        return -1;
    }
    serial_puts("[MINILIBC test] strncmp(prefix) PASSED\n");
    return 0;
}

/* ============================================================================
 * memset Tests
 * ============================================================================ */

static int test_memset_basic(void) {
    char buffer[20];
    memset(buffer, 'A', 10);
    buffer[10] = '\0';

    for (int i = 0; i < 10; i++) {
        if (buffer[i] != 'A') {
            serial_puts("[MINILIBC test] memset FAILED: byte not 'A'\n");
            return -1;
        }
    }
    serial_puts("[MINILIBC test] memset(basic) PASSED\n");
    return 0;
}

static int test_memset_zero(void) {
    char buffer[10] = "xxxxxxxx";
    memset(buffer, 0, 0);  /* Should do nothing */

    if (buffer[0] != 'x') {
        serial_puts("[MINILIBC test] memset(zero) FAILED: modified buffer\n");
        return -1;
    }
    serial_puts("[MINILIBC test] memset(zero) PASSED\n");
    return 0;
}

static int test_memset_zero_byte(void) {
    char buffer[10] = "xxxxxxxxx";
    memset(buffer, 0, 10);

    for (int i = 0; i < 10; i++) {
        if (buffer[i] != 0) {
            serial_puts("[MINILIBC test] memset(zero byte) FAILED\n");
            return -1;
        }
    }
    serial_puts("[MINILIBC test] memset(zero byte) PASSED\n");
    return 0;
}

static int test_memset_return_value(void) {
    char buffer[10];
    void *result = memset(buffer, 'X', 5);

    if (result != buffer) {
        serial_puts("[MINILIBC test] memset(return) FAILED: wrong return value\n");
        return -1;
    }
    serial_puts("[MINILIBC test] memset(return) PASSED\n");
    return 0;
}

static int test_memset_odd_size(void) {
    char buffer[20];
    memset(buffer, 'Z', 13);

    for (int i = 0; i < 13; i++) {
        if (buffer[i] != 'Z') {
            serial_puts("[MINILIBC test] memset(odd size) FAILED\n");
            return -1;
        }
    }
    serial_puts("[MINILIBC test] memset(odd size) PASSED\n");
    return 0;
}

static int test_memset_full_byte_range(void) {
    /* Use smaller buffer to avoid kernel memory interference */
    unsigned char buffer[128];
    memset(buffer, 0xAB, 128);

    for (int i = 0; i < 128; i++) {
        if (buffer[i] != 0xAB) {
            serial_puts("[MINILIBC test] memset(byte range) FAILED\n");
            return -1;
        }
    }
    serial_puts("[MINILIBC test] memset(byte range) PASSED\n");
    return 0;
}

/* ============================================================================
 * memcpy Tests
 * ============================================================================ */

static int test_memcpy_basic(void) {
    char src[20] = "hello world";
    char dest[20];
    char *result = memcpy(dest, src, 11);
    dest[11] = '\0';

    if (result != dest) {
        serial_puts("[MINILIBC test] memcpy FAILED: wrong return value\n");
        return -1;
    }
    if (strcmp(dest, "hello world") != 0) {
        serial_puts("[MINILIBC test] memcpy FAILED: copy mismatch\n");
        return -1;
    }
    serial_puts("[MINILIBC test] memcpy(basic) PASSED\n");
    return 0;
}

static int test_memcpy_zero(void) {
    char src[] = "test";
    char dest[] = "dest";
    memcpy(dest, src, 0);  /* Should do nothing */

    if (strcmp(dest, "dest") != 0) {
        serial_puts("[MINILIBC test] memcpy(zero) FAILED\n");
        return -1;
    }
    serial_puts("[MINILIBC test] memcpy(zero) PASSED\n");
    return 0;
}

static int test_memcpy_single_byte(void) {
    char src = 'X';
    char dest;
    memcpy(&dest, &src, 1);

    if (dest != 'X') {
        serial_puts("[MINILIBC test] memcpy(single byte) FAILED\n");
        return -1;
    }
    serial_puts("[MINILIBC test] memcpy(single byte) PASSED\n");
    return 0;
}

static int test_memcpy_return_value(void) {
    char src[10] = "test";
    char dest[10];
    void *result = memcpy(dest, src, 5);

    if (result != dest) {
        serial_puts("[MINILIBC test] memcpy(return) FAILED: wrong return value\n");
        return -1;
    }
    serial_puts("[MINILIBC test] memcpy(return) PASSED\n");
    return 0;
}

static int test_memcpy_exact_count(void) {
    char src[10];
    char dest[10];

    /* Fill src with pattern */
    for (int i = 0; i < 10; i++) {
        src[i] = (char)i;
    }

    /* Initialize dest with different pattern */
    for (int i = 0; i < 10; i++) {
        dest[i] = 0xFF - (char)i;
    }

    memcpy(dest, src, 10);

    for (int i = 0; i < 10; i++) {
        if (dest[i] != (char)i) {
            serial_puts("[MINILIBC test] memcpy(exact count) FAILED\n");
            return -1;
        }
    }
    serial_puts("[MINILIBC test] memcpy(exact count) PASSED\n");
    return 0;
}

static int test_memcpy_source_unchanged(void) {
    char src[20] = "preserve me";
    char dest[20];

    memcpy(dest, src, 12);

    if (strcmp(src, "preserve me") != 0) {
        serial_puts("[MINILIBC test] memcpy(src unchanged) FAILED\n");
        return -1;
    }
    serial_puts("[MINILIBC test] memcpy(src unchanged) PASSED\n");
    return 0;
}

static int test_memcpy_all_bytes(void) {
    /* Use smaller buffers to avoid kernel memory interference */
    unsigned char src[128];
    unsigned char dest[128];

    /* Fill with byte values 0-127 */
    for (int i = 0; i < 128; i++) {
        src[i] = (unsigned char)i;
    }

    memcpy(dest, src, 128);

    for (int i = 0; i < 128; i++) {
        if (dest[i] != (unsigned char)i) {
            serial_puts("[MINILIBC test] memcpy(byte range) FAILED at index ");
            serial_put_hex(i);
            serial_puts("\n");
            return -1;
        }
    }
    serial_puts("[MINILIBC test] memcpy(byte range) PASSED\n");
    return 0;
}

/* ============================================================================
 * snprintf Tests
 * ============================================================================ */

static int test_snprintf_basic_string(void) {
    char buf[32];
    int ret = snprintf(buf, sizeof(buf), "hello");

    if (ret != 5 || strcmp(buf, "hello") != 0) {
        serial_puts("[MINILIBC test] snprintf(basic string) FAILED\n");
        return -1;
    }
    serial_puts("[MINILIBC test] snprintf(basic string) PASSED\n");
    return 0;
}

static int test_snprintf_truncation(void) {
    char buf[5];
    int ret = snprintf(buf, sizeof(buf), "hello world");

    /* C99: returns what WOULD be written (11), not truncated length */
    if (ret != 11 || strcmp(buf, "hell") != 0) {
        serial_puts("[SNPRINTF test] truncation FAILED: ret=");
        serial_put_hex(ret);
        serial_puts(" buf='");
        serial_puts(buf);
        serial_puts("'\n");
        return -1;
    }
    serial_puts("[MINILIBC test] snprintf(truncation) PASSED\n");
    return 0;
}

static int test_snprintf_format_d(void) {
    char buf[32];
    int ret = snprintf(buf, sizeof(buf), "value: %d", 42);

    if (ret != 9 || strcmp(buf, "value: 42") != 0) {
        serial_puts("[SNPRINTF test] %%d FAILED\n");
        return -1;
    }
    serial_puts("[MINILIBC test] snprintf(%d) PASSED\n");
    return 0;
}

static int test_snprintf_format_negative(void) {
    char buf[32];
    int ret = snprintf(buf, sizeof(buf), "%d", -123);

    if (ret != 4 || strcmp(buf, "-123") != 0) {
        serial_puts("[SNPRINTF test] negative FAILED\n");
        return -1;
    }
    serial_puts("[MINILIBC test] snprintf(negative) PASSED\n");
    return 0;
}

static int test_snprintf_format_x(void) {
    char buf[32];
    int ret = snprintf(buf, sizeof(buf), "0x%x", 255);

    if (ret != 4 || strcmp(buf, "0xff") != 0) {
        serial_puts("[SNPRINTF test] %%x FAILED\n");
        return -1;
    }
    serial_puts("[MINILIBC test] snprintf(%x) PASSED\n");
    return 0;
}

static int test_snprintf_format_X(void) {
    char buf[32];
    int ret = snprintf(buf, sizeof(buf), "0x%X", 255);

    if (ret != 4 || strcmp(buf, "0xFF") != 0) {
        serial_puts("[SNPRINTF test] %%X FAILED\n");
        return -1;
    }
    serial_puts("[MINILIBC test] snprintf(%X) PASSED\n");
    return 0;
}

static int test_snprintf_format_s(void) {
    char buf[32];
    int ret = snprintf(buf, sizeof(buf), "msg: %s", "test");

    if (ret != 9 || strcmp(buf, "msg: test") != 0) {
        serial_puts("[SNPRINTF test] %%s FAILED\n");
        return -1;
    }
    serial_puts("[MINILIBC test] snprintf(%s) PASSED\n");
    return 0;
}

static int test_snprintf_format_c(void) {
    char buf[32];
    int ret = snprintf(buf, sizeof(buf), "char: %c", 'Z');

    if (ret != 7 || strcmp(buf, "char: Z") != 0) {
        serial_puts("[SNPRINTF test] %%c FAILED\n");
        return -1;
    }
    serial_puts("[MINILIBC test] snprintf(%c) PASSED\n");
    return 0;
}

static int test_snprintf_format_p(void) {
    char buf[32];
    int ret = snprintf(buf, sizeof(buf), "%p", (void *)0x1234);

    if (ret < 6 || buf[0] != '0' || buf[1] != 'x') {
        serial_puts("[SNPRINTF test] %p FAILED\n");
        return -1;
    }
    serial_puts("[MINILIBC test] snprintf(%p) PASSED\n");
    return 0;
}

static int test_snprintf_format_percent(void) {
    char buf[32];
    int ret = snprintf(buf, sizeof(buf), "100%%");

    if (ret != 4 || strcmp(buf, "100%") != 0) {
        serial_puts("[SNPRINTF test] %% FAILED\n");
        return -1;
    }
    serial_puts("[MINILIBC test] snprintf(%%) PASSED\n");
    return 0;
}

static int test_snprintf_multiple_specs(void) {
    char buf[64];
    int ret = snprintf(buf, sizeof(buf), "%s: %d (0x%x)", "test", 10, 255);

    /* "test: 10 (0xff)" = 15 chars */
    if (ret != 15 || strcmp(buf, "test: 10 (0xff)") != 0) {
        serial_puts("[SNPRINTF test] multiple specs FAILED: ret=");
        serial_put_hex(ret);
        serial_puts(" buf='");
        serial_puts(buf);
        serial_puts("'\n");
        return -1;
    }
    serial_puts("[MINILIBC test] snprintf(multiple) PASSED\n");
    return 0;
}

static int test_snprintf_zero_size(void) {
    char buf[4] = "xxx";
    int ret = snprintf(buf, 0, "hello");

    /* Should not write anything, return length */
    if (ret != 5 || strcmp(buf, "xxx") != 0) {
        serial_puts("[SNPRINTF test] zero size FAILED\n");
        return -1;
    }
    serial_puts("[MINILIBC test] snprintf(zero size) PASSED\n");
    return 0;
}

/**
 * run_minilibc_tests - Run minilibc string library tests
 *
 * Tests strlen, strcpy, strcmp, strncmp, memset, memcpy, snprintf functions.
 *
 * Returns: 0 on success, -1 on failure
 */
int run_minilibc_tests(void) {
    serial_puts("\n========================================\n");
    serial_puts("  Minilibc Test Suite (Extended)\n");
    serial_puts("========================================\n");
    serial_puts("\n");

    /* Run strlen tests */
    if (test_strlen_empty() != 0) return -1;
    if (test_strlen_basic() != 0) return -1;
    if (test_strlen_single_char() != 0) return -1;
    if (test_strlen_with_spaces() != 0) return -1;
    if (test_strlen_special_chars() != 0) return -1;

    /* Run strcpy tests */
    if (test_strcpy_basic() != 0) return -1;
    if (test_strcpy_empty_src() != 0) return -1;
    if (test_strcpy_single_char() != 0) return -1;
    if (test_strcopy_with_spaces() != 0) return -1;

    /* Run strcmp tests */
    if (test_strcmp_equal() != 0) return -1;
    if (test_strcmp_both_empty() != 0) return -1;
    if (test_strcmp_one_empty() != 0) return -1;
    if (test_strcmp_less() != 0) return -1;
    if (test_strcmp_greater() != 0) return -1;
    if (test_strcmp_prefix() != 0) return -1;
    if (test_strcmp_case_sensitive() != 0) return -1;

    /* Run strncmp tests */
    if (test_strncmp_equal() != 0) return -1;
    if (test_strncmp_equal_prefix() != 0) return -1;
    if (test_strncmp_zero() != 0) return -1;
    if (test_strncmp_both_empty() != 0) return -1;
    if (test_strncmp_limit_shorter() != 0) return -1;
    if (test_strncmp_one_is_prefix() != 0) return -1;

    /* Run memset tests */
    if (test_memset_basic() != 0) return -1;
    if (test_memset_zero() != 0) return -1;
    if (test_memset_zero_byte() != 0) return -1;
    if (test_memset_return_value() != 0) return -1;
    if (test_memset_odd_size() != 0) return -1;
    if (test_memset_full_byte_range() != 0) return -1;

    /* Run memcpy tests */
    if (test_memcpy_basic() != 0) return -1;
    if (test_memcpy_zero() != 0) return -1;
    if (test_memcpy_single_byte() != 0) return -1;
    if (test_memcpy_return_value() != 0) return -1;
    if (test_memcpy_exact_count() != 0) return -1;
    if (test_memcpy_source_unchanged() != 0) return -1;
    if (test_memcpy_all_bytes() != 0) return -1;

    /* Run snprintf tests */
    if (test_snprintf_basic_string() != 0) return -1;
    if (test_snprintf_truncation() != 0) return -1;
    if (test_snprintf_format_d() != 0) return -1;
    if (test_snprintf_format_negative() != 0) return -1;
    if (test_snprintf_format_x() != 0) return -1;
    if (test_snprintf_format_X() != 0) return -1;
    if (test_snprintf_format_s() != 0) return -1;
    if (test_snprintf_format_c() != 0) return -1;
    if (test_snprintf_format_p() != 0) return -1;
    if (test_snprintf_format_percent() != 0) return -1;
    if (test_snprintf_multiple_specs() != 0) return -1;
    if (test_snprintf_zero_size() != 0) return -1;

    serial_puts("\n========================================\n");
    serial_puts("  Minilibc: All tests PASSED\n");
    serial_puts("========================================\n");
    serial_puts("\n");

    return 0;
}
#endif  /* CONFIG_MINILIBC_TESTS */

/* External test functions */
extern int run_pmm_tests(void);
extern int run_minilibc_tests(void);
extern int run_slab_tests(void);
extern int run_spinlock_tests(void);
extern int run_nk_fault_injection_tests(void);
extern int run_apic_timer_tests(void);
extern int run_boot_tests(void);
extern int run_smp_tests(void);
extern int run_pcd_tests(void);
extern int run_nested_kernel_invariants_tests(void);
extern int run_readonly_visibility_tests(void);
extern int run_usermode_tests(void);

/* Test registry array */
const test_case_t test_registry[] = {
#if CONFIG_TESTS_PMM
    {
        .name = "pmm",
        .description = "Physical memory manager allocation tests",
        .run_func = run_pmm_tests,
        .enabled = 1,
        .auto_run = 1  /* Auto-run after pmm_init() */
    },
#endif
#if CONFIG_TESTS_SLAB
    {
        .name = "slab",
        .description = "Slab allocator small object allocation tests",
        .run_func = run_slab_tests,
        .enabled = 1,
        .auto_run = 1  /* Auto-run after slab_init() */
    },
#endif
#if CONFIG_SPINLOCK_TESTS
    {
        .name = "spinlock",
        .description = "Spin lock and read-write lock synchronization tests",
        .run_func = run_spinlock_tests,
        .enabled = 1,
        .auto_run = 1  /* Auto-run after AP startup */
    },
#endif
#if CONFIG_TESTS_APIC_TIMER
    {
        .name = "timer",
        .description = "APIC timer interrupt-driven tests",
        .run_func = run_apic_timer_tests,
        .enabled = 1,
        .auto_run = 1  /* Auto-run after BSP init */
    },
#endif
#if CONFIG_NK_FAULT_INJECTION_TESTS
    {
        .name = "nk_fault_injection",
        .description = "Nested kernel fault injection tests (destructive)",
        .run_func = run_nk_fault_injection_tests,
        .enabled = 1,
        .auto_run = 0  /* Manual only (destructive test) */
    },
#endif
#if CONFIG_TESTS_BOOT
    {
        .name = "boot",
        .description = "Basic kernel boot verification",
        .run_func = run_boot_tests,
        .enabled = 1,
        .auto_run = 0  /* Manual only */
    },
#endif
#if CONFIG_TESTS_SMP
    {
        .name = "smp",
        .description = "SMP startup and multi-CPU verification",
        .run_func = run_smp_tests,
        .enabled = 1,
        .auto_run = 0  /* Manual only */
    },
#endif
#if CONFIG_TESTS_PCD
    {
        .name = "pcd",
        .description = "Page Control Data initialization and tracking",
        .run_func = run_pcd_tests,
        .enabled = 1,
        .auto_run = 0  /* Manual only */
    },
#endif
#if CONFIG_TESTS_NK_INVARIANTS
    {
        .name = "nested_kernel_invariants",
        .description = "Nested Kernel invariants (ASPLOS '15)",
        .run_func = run_nested_kernel_invariants_tests,
        .enabled = 1,
        .auto_run = 0  /* Manual only */
    },
#endif
#if CONFIG_TESTS_NK_READONLY_VISIBILITY
    {
        .name = "readonly_visibility",
        .description = "Read-only mapping visibility for nested kernel",
        .run_func = run_readonly_visibility_tests,
        .enabled = 1,
        .auto_run = 0  /* Manual only */
    },
#endif
#if CONFIG_TESTS_MINILIBC
    {
        .name = "minilibc",
        .description = "Minimal string library tests (49 tests: strlen, strcpy, strcmp, strncmp, memset, memcpy, snprintf)",
        .run_func = run_minilibc_tests,
        .enabled = 1,
        .auto_run = 1  /* Auto-run after kernel init */
    },
#endif
#if CONFIG_USERMODE_TEST
    {
        .name = "usermode",
        .description = "User mode syscall and ring 3 execution tests",
        .run_func = run_usermode_tests,
        .enabled = 1,
        .auto_run = 0  /* Manual only */
    },
#endif
    { .name = NULL }  /* Sentinel */
};

/* ============================================================================
 * Framework Implementation
 * ============================================================================ */

/**
 * test_framework_init() - Initialize test framework from cmdline
 */
void test_framework_init(void) {
    const char *test_value;

    serial_puts("[TEST] Initializing test framework...\n");

    /* Initialize tests_run tracking array */
    for (int i = 0; i < MAX_TESTS; i++) {
        tests_run[i] = 0;
    }
    tests_run_count = 0;

    /* Parse test= parameter from cmdline */
    test_value = cmdline_get_value("test");

    if (test_value == NULL) {
        test_mode = TEST_MODE_NONE;
        serial_puts("[TEST] No test= parameter (default: no tests run)\n");
        return;
    }

    serial_puts("[TEST] test=");
    serial_puts(test_value);
    serial_puts("\n");

    /* Determine test mode */
    if (strcmp(test_value, "all") == 0) {
        test_mode = TEST_MODE_ALL;
        serial_puts("[TEST] Mode: ALL (run all auto_run tests)\n");
    } else if (strcmp(test_value, "unified") == 0) {
        test_mode = TEST_MODE_UNIFIED;
        serial_puts("[TEST] Mode: UNIFIED (run all selected tests at end)\n");
    } else {
        test_mode = TEST_MODE_SPECIFIC;
        selected_test_name = test_value;
        serial_puts("[TEST] Mode: SPECIFIC (test=");
        serial_puts(selected_test_name);
        serial_puts(")\n");
    }
}

/**
 * test_should_run() - Check if test should run in auto mode
 * @name: Test name
 *
 * Returns: 1 if test should run, 0 otherwise
 */
int test_should_run(const char *name) {
    /* No test= parameter: no tests run */
    if (test_mode == TEST_MODE_NONE) {
        return 0;
    }

    /* Unified mode: tests run at end, not during init */
    if (test_mode == TEST_MODE_UNIFIED) {
        return 0;  /* Will be run by test_run_unified() */
    }

    /* test=all: run all enabled tests with auto_run=1 */
    if (test_mode == TEST_MODE_ALL) {
        /* Find test in registry and check auto_run flag */
        for (int i = 0; test_registry[i].name != NULL; i++) {
            if (strcmp(test_registry[i].name, name) == 0) {
                return test_registry[i].enabled && test_registry[i].auto_run;
            }
        }
        return 0;  /* Test not found */
    }

    /* test=<name>: run only the specified test */
    if (test_mode == TEST_MODE_SPECIFIC) {
        return (strcmp(selected_test_name, name) == 0);
    }

    return 0;
}

/**
 * test_run_by_name() - Execute test by name with fail-fast
 * @name: Test name
 *
 * Returns: 0 on success, non-zero on failure (but shutdown occurs first)
 */
int test_run_by_name(const char *name) {
    const test_case_t *test = NULL;

    serial_puts("[TEST] Running test: ");
    serial_puts(name);
    serial_puts("\n");

    /* Find test in registry */
    for (int i = 0; test_registry[i].name != NULL; i++) {
        if (strcmp(test_registry[i].name, name) == 0) {
            test = &test_registry[i];
            break;
        }
    }

    if (test == NULL) {
        serial_puts("[TEST] ERROR: Test '");
        serial_puts(name);
        serial_puts("' not found in registry\n");
        return -1;
    }

    if (!test->enabled) {
        serial_puts("[TEST] ERROR: Test '");
        serial_puts(name);
        serial_puts("' is not enabled (check CONFIG_ flag)\n");
        return -1;
    }

    /* Mark test as run */
    if (tests_run_count < MAX_TESTS) {
        tests_run[tests_run_count++] = 1;
    }

    /* Run test */
    serial_puts("[TEST] Description: ");
    serial_puts(test->description);
    serial_puts("\n");

    int result = test->run_func();

    if (result == 0) {
        serial_puts("[TEST] PASSED: ");
        serial_puts(name);
        serial_puts("\n");
    } else {
        serial_puts("[TEST] FAILED: ");
        serial_puts(name);
        serial_puts(" (failures: ");
        serial_put_hex(result);
        serial_puts(")\n");
        serial_puts("[TEST] FAILURE - System shutting down\n");
        system_shutdown();
        /* Never returns */
    }

    return result;
}

/**
 * test_run_unified() - Run all selected tests at unified point
 *
 * Returns: 0 on success, non-zero if any test failed
 */
int test_run_unified(void) {
    int failures = 0;

    /* Only run if in unified mode */
    if (test_mode != TEST_MODE_UNIFIED) {
        return 0;
    }

    serial_puts("\n");
    serial_puts("========================================\n");
    serial_puts("UNIFIED TEST EXECUTION\n");
    serial_puts("========================================\n");
    serial_puts("\n");

    /* Run all enabled tests that haven't run yet */
    for (int i = 0; test_registry[i].name != NULL; i++) {
        const test_case_t *test = &test_registry[i];

        if (!test->enabled) {
            continue;  /* Skip disabled tests */
        }

        /* Check if already run (shouldn't happen in unified mode, but be safe) */
        int already_run = 0;
        for (int j = 0; j < tests_run_count; j++) {
            if (tests_run[j]) {
                already_run = 1;
                break;
            }
        }
        if (already_run) {
            continue;
        }

        /* Run the test */
        serial_puts("[TEST] [");
        serial_put_hex(i + 1);
        serial_puts("/");
        serial_put_hex(i + 1);  /* Will update after counting total */
        serial_puts("] Running: ");
        serial_puts(test->name);
        serial_puts("\n");

        int result = test->run_func();

        if (result != 0) {
            serial_puts("[TEST] FAILED: ");
            serial_puts(test->name);
            serial_puts("\n");
            failures++;
            /* Continue running other tests to see all failures */
        } else {
            serial_puts("[TEST] PASSED: ");
            serial_puts(test->name);
            serial_puts("\n");
        }

        serial_puts("\n");
    }

    /* Summary */
    serial_puts("========================================\n");
    if (failures == 0) {
        serial_puts("UNIFIED: ALL TESTS PASSED\n");
    } else {
        serial_puts("UNIFIED: SOME TESTS FAILED\n");
        serial_puts("Failures: ");
        serial_put_hex(failures);
        serial_puts("\n");
    }
    serial_puts("========================================\n");
    serial_puts("\n");

    return failures;
}
