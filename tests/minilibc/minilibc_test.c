/* Emergence Kernel - Minilibc String/Memory Tests */

#include <stdint.h>
#include <stddef.h>
#include "test_minilibc.h"
#include "kernel/test.h"
#include "arch/x86_64/serial.h"
#include "include/string.h"

#if CONFIG_TESTS_MINILIBC

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

/* ============================================================================
 * Main Test Runner
 * ============================================================================ */

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

#endif  /* CONFIG_TESTS_MINILIBC */

/* ============================================================================
 * Test Wrapper
 * ============================================================================ */

#if CONFIG_TESTS_MINILIBC
void test_minilibc(void) {
    if (test_should_run("minilibc")) {
        test_run_by_name("minilibc");
    }
}
#else
void test_minilibc(void) { }
#endif
