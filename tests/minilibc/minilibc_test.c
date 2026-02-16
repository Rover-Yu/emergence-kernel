/* Emergence Kernel - Minilibc String/Memory Tests */

#include <stdint.h>
#include <stddef.h>
#include "test_minilibc.h"
#include "kernel/test.h"
#include "kernel/klog.h"
#include "arch/x86_64/serial.h"
#include "include/string.h"

#if CONFIG_TESTS_MINILIBC

/* ============================================================================
 * strlen Tests
 * ============================================================================ */

static int test_strlen_empty(void) {
    size_t len = strlen("");
    if (len != 0) {
        klog_error("MINILIBC_TEST", "strlen(\"\") FAILED: expected 0");
        return -1;
    }
    klog_info("MINILIBC_TEST", "strlen(\"\") PASSED");
    return 0;
}

static int test_strlen_basic(void) {
    size_t len = strlen("hello");
    if (len != 5) {
        klog_error("MINILIBC_TEST", "strlen(\"hello\") FAILED: expected 5");
        return -1;
    }
    klog_info("MINILIBC_TEST", "strlen(\"hello\") PASSED");
    return 0;
}

static int test_strlen_single_char(void) {
    size_t len = strlen("A");
    if (len != 1) {
        klog_error("MINILIBC_TEST", "strlen(\"A\") FAILED: expected 1");
        return -1;
    }
    klog_info("MINILIBC_TEST", "strlen(\"A\") PASSED");
    return 0;
}

static int test_strlen_with_spaces(void) {
    size_t len = strlen("hello world");
    if (len != 11) {
        klog_error("MINILIBC_TEST", "strlen(\"hello world\") FAILED: expected 11");
        return -1;
    }
    klog_info("MINILIBC_TEST", "strlen(\"hello world\") PASSED");
    return 0;
}

static int test_strlen_special_chars(void) {
    size_t len = strlen("\n\t\x01");
    if (len != 3) {
        klog_error("MINILIBC_TEST", "strlen(special chars) FAILED: expected 3");
        return -1;
    }
    klog_info("MINILIBC_TEST", "strlen(special chars) PASSED");
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
        klog_error("MINILIBC_TEST", "strcpy FAILED: wrong return value");
        return -1;
    }
    if (strcmp(dest, src) != 0) {
        klog_error("MINILIBC_TEST", "strcpy FAILED: copy mismatch");
        return -1;
    }
    klog_info("MINILIBC_TEST", "strcpy(basic) PASSED");
    return 0;
}

static int test_strcpy_empty_src(void) {
    char dest[20] = "garbage";
    char *result = strcpy(dest, "");

    if (result != dest) {
        klog_error("MINILIBC_TEST", "strcpy(empty) FAILED: wrong return value");
        return -1;
    }
    if (dest[0] != '\0') {
        klog_error("MINILIBC_TEST", "strcpy(empty) FAILED: not null terminated");
        return -1;
    }
    klog_info("MINILIBC_TEST", "strcpy(empty) PASSED");
    return 0;
}

static int test_strcpy_single_char(void) {
    char dest[20];
    char *result = strcpy(dest, "X");

    if (result != dest) {
        klog_error("MINILIBC_TEST", "strcpy(single) FAILED: wrong return value");
        return -1;
    }
    if (dest[0] != 'X' || dest[1] != '\0') {
        klog_error("MINILIBC_TEST", "strcpy(single) FAILED: incorrect copy");
        return -1;
    }
    klog_info("MINILIBC_TEST", "strcpy(single) PASSED");
    return 0;
}

static int test_strcopy_with_spaces(void) {
    char dest[20];
    strcpy(dest, "hello world");

    if (strcmp(dest, "hello world") != 0) {
        klog_error("MINILIBC_TEST", "strcpy(spaces) FAILED: copy mismatch");
        return -1;
    }
    klog_info("MINILIBC_TEST", "strcpy(spaces) PASSED");
    return 0;
}

/* ============================================================================
 * strcmp Tests
 * ============================================================================ */

static int test_strcmp_equal(void) {
    if (strcmp("hello", "hello") != 0) {
        klog_error("MINILIBC_TEST", "strcmp(equal) FAILED");
        return -1;
    }
    klog_info("MINILIBC_TEST", "strcmp(equal) PASSED");
    return 0;
}

static int test_strcmp_both_empty(void) {
    if (strcmp("", "") != 0) {
        klog_error("MINILIBC_TEST", "strcmp(both empty) FAILED");
        return -1;
    }
    klog_info("MINILIBC_TEST", "strcmp(both empty) PASSED");
    return 0;
}

static int test_strcmp_one_empty(void) {
    if (strcmp("", "abc") >= 0) {
        klog_error("MINILIBC_TEST", "strcmp(empty < nonempty) FAILED");
        return -1;
    }
    if (strcmp("abc", "") <= 0) {
        klog_error("MINILIBC_TEST", "strcmp(nonempty > empty) FAILED");
        return -1;
    }
    klog_info("MINILIBC_TEST", "strcmp(one empty) PASSED");
    return 0;
}

static int test_strcmp_less(void) {
    if (strcmp("abc", "def") >= 0) {
        klog_error("MINILIBC_TEST", "strcmp(less) FAILED");
        return -1;
    }
    klog_info("MINILIBC_TEST", "strcmp(less) PASSED");
    return 0;
}

static int test_strcmp_greater(void) {
    if (strcmp("xyz", "abc") <= 0) {
        klog_error("MINILIBC_TEST", "strcmp(greater) FAILED");
        return -1;
    }
    klog_info("MINILIBC_TEST", "strcmp(greater) PASSED");
    return 0;
}

static int test_strcmp_prefix(void) {
    if (strcmp("abc", "abcd") >= 0) {
        klog_error("MINILIBC_TEST", "strcmp(prefix) FAILED");
        return -1;
    }
    klog_info("MINILIBC_TEST", "strcmp(prefix) PASSED");
    return 0;
}

static int test_strcmp_case_sensitive(void) {
    if (strcmp("ABC", "abc") == 0) {
        klog_error("MINILIBC_TEST", "strcmp(case) FAILED: should be case sensitive");
        return -1;
    }
    klog_info("MINILIBC_TEST", "strcmp(case) PASSED");
    return 0;
}

/* ============================================================================
 * strncmp Tests
 * ============================================================================ */

static int test_strncmp_equal(void) {
    if (strncmp("hello", "hello", 10) != 0) {
        klog_error("MINILIBC_TEST", "strncmp(equal) FAILED");
        return -1;
    }
    klog_info("MINILIBC_TEST", "strncmp(equal) PASSED");
    return 0;
}

static int test_strncmp_equal_prefix(void) {
    if (strncmp("hello", "helium", 3) != 0) {
        klog_error("MINILIBC_TEST", "strncmp(equal prefix) FAILED");
        return -1;
    }
    klog_info("MINILIBC_TEST", "strncmp(equal prefix) PASSED");
    return 0;
}

static int test_strncmp_zero(void) {
    if (strncmp("abc", "xyz", 0) != 0) {
        klog_error("MINILIBC_TEST", "strncmp(zero limit) FAILED");
        return -1;
    }
    klog_info("MINILIBC_TEST", "strncmp(zero limit) PASSED");
    return 0;
}

static int test_strncmp_both_empty(void) {
    if (strncmp("", "", 10) != 0) {
        klog_error("MINILIBC_TEST", "strncmp(both empty) FAILED");
        return -1;
    }
    klog_info("MINILIBC_TEST", "strncmp(both empty) PASSED");
    return 0;
}

static int test_strncmp_limit_shorter(void) {
    if (strncmp("abc", "xyz", 1) == 0) {
        klog_error("MINILIBC_TEST", "strncmp(limit shorter) FAILED");
        return -1;
    }
    klog_info("MINILIBC_TEST", "strncmp(limit shorter) PASSED");
    return 0;
}

static int test_strncmp_one_is_prefix(void) {
    if (strncmp("abc", "abcd", 4) >= 0) {
        klog_error("MINILIBC_TEST", "strncmp(prefix) FAILED");
        return -1;
    }
    klog_info("MINILIBC_TEST", "strncmp(prefix) PASSED");
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
            klog_error("MINILIBC_TEST", "memset FAILED: byte not 'A'");
            return -1;
        }
    }
    klog_info("MINILIBC_TEST", "memset(basic) PASSED");
    return 0;
}

static int test_memset_zero(void) {
    char buffer[10] = "xxxxxxxx";
    memset(buffer, 0, 0);  /* Should do nothing */

    if (buffer[0] != 'x') {
        klog_error("MINILIBC_TEST", "memset(zero) FAILED: modified buffer");
        return -1;
    }
    klog_info("MINILIBC_TEST", "memset(zero) PASSED");
    return 0;
}

static int test_memset_zero_byte(void) {
    char buffer[10] = "xxxxxxxxx";
    memset(buffer, 0, 10);

    for (int i = 0; i < 10; i++) {
        if (buffer[i] != 0) {
            klog_error("MINILIBC_TEST", "memset(zero byte) FAILED");
            return -1;
        }
    }
    klog_info("MINILIBC_TEST", "memset(zero byte) PASSED");
    return 0;
}

static int test_memset_return_value(void) {
    char buffer[10];
    void *result = memset(buffer, 'X', 5);

    if (result != buffer) {
        klog_error("MINILIBC_TEST", "memset(return) FAILED: wrong return value");
        return -1;
    }
    klog_info("MINILIBC_TEST", "memset(return) PASSED");
    return 0;
}

static int test_memset_odd_size(void) {
    char buffer[20];
    memset(buffer, 'Z', 13);

    for (int i = 0; i < 13; i++) {
        if (buffer[i] != 'Z') {
            klog_error("MINILIBC_TEST", "memset(odd size) FAILED");
            return -1;
        }
    }
    klog_info("MINILIBC_TEST", "memset(odd size) PASSED");
    return 0;
}

static int test_memset_full_byte_range(void) {
    /* Use smaller buffer to avoid kernel memory interference */
    unsigned char buffer[128];
    memset(buffer, 0xAB, 128);

    for (int i = 0; i < 128; i++) {
        if (buffer[i] != 0xAB) {
            klog_error("MINILIBC_TEST", "memset(byte range) FAILED");
            return -1;
        }
    }
    klog_info("MINILIBC_TEST", "memset(byte range) PASSED");
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
        klog_error("MINILIBC_TEST", "memcpy FAILED: wrong return value");
        return -1;
    }
    if (strcmp(dest, "hello world") != 0) {
        klog_error("MINILIBC_TEST", "memcpy FAILED: copy mismatch");
        return -1;
    }
    klog_info("MINILIBC_TEST", "memcpy(basic) PASSED");
    return 0;
}

static int test_memcpy_zero(void) {
    char src[] = "test";
    char dest[] = "dest";
    memcpy(dest, src, 0);  /* Should do nothing */

    if (strcmp(dest, "dest") != 0) {
        klog_error("MINILIBC_TEST", "memcpy(zero) FAILED");
        return -1;
    }
    klog_info("MINILIBC_TEST", "memcpy(zero) PASSED");
    return 0;
}

static int test_memcpy_single_byte(void) {
    char src = 'X';
    char dest;
    memcpy(&dest, &src, 1);

    if (dest != 'X') {
        klog_error("MINILIBC_TEST", "memcpy(single byte) FAILED");
        return -1;
    }
    klog_info("MINILIBC_TEST", "memcpy(single byte) PASSED");
    return 0;
}

static int test_memcpy_return_value(void) {
    char src[10] = "test";
    char dest[10];
    void *result = memcpy(dest, src, 5);

    if (result != dest) {
        klog_error("MINILIBC_TEST", "memcpy(return) FAILED: wrong return value");
        return -1;
    }
    klog_info("MINILIBC_TEST", "memcpy(return) PASSED");
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
            klog_error("MINILIBC_TEST", "memcpy(exact count) FAILED");
            return -1;
        }
    }
    klog_info("MINILIBC_TEST", "memcpy(exact count) PASSED");
    return 0;
}

static int test_memcpy_source_unchanged(void) {
    char src[20] = "preserve me";
    char dest[20];

    memcpy(dest, src, 12);

    if (strcmp(src, "preserve me") != 0) {
        klog_error("MINILIBC_TEST", "memcpy(src unchanged) FAILED");
        return -1;
    }
    klog_info("MINILIBC_TEST", "memcpy(src unchanged) PASSED");
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
            klog_error("MINILIBC_TEST", "memcpy(byte range) FAILED at index %x", i);
            return -1;
        }
    }
    klog_info("MINILIBC_TEST", "memcpy(byte range) PASSED");
    return 0;
}

/* ============================================================================
 * snprintf Tests
 * ============================================================================ */

static int test_snprintf_basic_string(void) {
    char buf[32];
    int ret = snprintf(buf, sizeof(buf), "hello");

    if (ret != 5 || strcmp(buf, "hello") != 0) {
        klog_error("MINILIBC_TEST", "snprintf(basic string) FAILED");
        return -1;
    }
    klog_info("MINILIBC_TEST", "snprintf(basic string) PASSED");
    return 0;
}

static int test_snprintf_truncation(void) {
    char buf[5];
    int ret = snprintf(buf, sizeof(buf), "hello world");

    /* C99: returns what WOULD be written (11), not truncated length */
    if (ret != 11 || strcmp(buf, "hell") != 0) {
        klog_error("MINILIBC_TEST", "snprintf(truncation) FAILED: ret=%x buf='%s'", ret, buf);
        return -1;
    }
    klog_info("MINILIBC_TEST", "snprintf(truncation) PASSED");
    return 0;
}

static int test_snprintf_format_d(void) {
    char buf[32];
    int ret = snprintf(buf, sizeof(buf), "value: %d", 42);

    if (ret != 9 || strcmp(buf, "value: 42") != 0) {
        klog_error("MINILIBC_TEST", "snprintf(%%d) FAILED");
        return -1;
    }
    klog_info("MINILIBC_TEST", "snprintf(%%d) PASSED");
    return 0;
}

static int test_snprintf_format_negative(void) {
    char buf[32];
    int ret = snprintf(buf, sizeof(buf), "%d", -123);

    if (ret != 4 || strcmp(buf, "-123") != 0) {
        klog_error("MINILIBC_TEST", "snprintf(negative) FAILED");
        return -1;
    }
    klog_info("MINILIBC_TEST", "snprintf(negative) PASSED");
    return 0;
}

static int test_snprintf_format_x(void) {
    char buf[32];
    int ret = snprintf(buf, sizeof(buf), "0x%x", 255);

    if (ret != 4 || strcmp(buf, "0xff") != 0) {
        klog_error("MINILIBC_TEST", "snprintf(%%x) FAILED");
        return -1;
    }
    klog_info("MINILIBC_TEST", "snprintf(%%x) PASSED");
    return 0;
}

static int test_snprintf_format_X(void) {
    char buf[32];
    int ret = snprintf(buf, sizeof(buf), "0x%X", 255);

    if (ret != 4 || strcmp(buf, "0xFF") != 0) {
        klog_error("MINILIBC_TEST", "snprintf(%%X) FAILED");
        return -1;
    }
    klog_info("MINILIBC_TEST", "snprintf(%%X) PASSED");
    return 0;
}

static int test_snprintf_format_s(void) {
    char buf[32];
    int ret = snprintf(buf, sizeof(buf), "msg: %s", "test");

    if (ret != 9 || strcmp(buf, "msg: test") != 0) {
        klog_error("MINILIBC_TEST", "snprintf(%%s) FAILED");
        return -1;
    }
    klog_info("MINILIBC_TEST", "snprintf(%%s) PASSED");
    return 0;
}

static int test_snprintf_format_c(void) {
    char buf[32];
    int ret = snprintf(buf, sizeof(buf), "char: %c", 'Z');

    if (ret != 7 || strcmp(buf, "char: Z") != 0) {
        klog_error("MINILIBC_TEST", "snprintf(%%c) FAILED");
        return -1;
    }
    klog_info("MINILIBC_TEST", "snprintf(%%c) PASSED");
    return 0;
}

static int test_snprintf_format_p(void) {
    char buf[32];
    int ret = snprintf(buf, sizeof(buf), "%p", (void *)0x1234);

    if (ret < 6 || buf[0] != '0' || buf[1] != 'x') {
        klog_error("MINILIBC_TEST", "snprintf(%%p) FAILED");
        return -1;
    }
    klog_info("MINILIBC_TEST", "snprintf(%%p) PASSED");
    return 0;
}

static int test_snprintf_format_percent(void) {
    char buf[32];
    int ret = snprintf(buf, sizeof(buf), "100%%");

    if (ret != 4 || strcmp(buf, "100%") != 0) {
        klog_error("MINILIBC_TEST", "snprintf(%%) FAILED");
        return -1;
    }
    klog_info("MINILIBC_TEST", "snprintf(%%) PASSED");
    return 0;
}

static int test_snprintf_multiple_specs(void) {
    char buf[64];
    int ret = snprintf(buf, sizeof(buf), "%s: %d (0x%x)", "test", 10, 255);

    /* "test: 10 (0xff)" = 15 chars */
    if (ret != 15 || strcmp(buf, "test: 10 (0xff)") != 0) {
        klog_error("MINILIBC_TEST", "snprintf(multiple) FAILED: ret=%x buf='%s'", ret, buf);
        return -1;
    }
    klog_info("MINILIBC_TEST", "snprintf(multiple) PASSED");
    return 0;
}

static int test_snprintf_zero_size(void) {
    char buf[4] = "xxx";
    int ret = snprintf(buf, 0, "hello");

    /* Should not write anything, return length */
    if (ret != 5 || strcmp(buf, "xxx") != 0) {
        klog_error("MINILIBC_TEST", "snprintf(zero size) FAILED");
        return -1;
    }
    klog_info("MINILIBC_TEST", "snprintf(zero size) PASSED");
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
    klog_info("MINILIBC_TEST", "=== Minilibc Test Suite (Extended) ===");

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

    klog_info("MINILIBC_TEST", "Minilibc: All tests PASSED");

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
