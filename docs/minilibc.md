# Minilibc - Minimal C Library for Emergence Kernel

## Overview

Minilibc is a minimal C library implementation for the Emergence Kernel. It provides essential string and memory manipulation functions without external dependencies.

## Design Goals

1. **Self-contained**: No external libc dependencies
2. **Kernel-safe**: No dynamic memory allocation (stack-only)
3. **Simple**: Straightforward implementations for clarity
4. **Tested**: Comprehensive test coverage

## Available Functions

### String Functions

- `size_t strlen(const char *s)` - Calculate string length
- `char *strcpy(char *dest, const char *src)` - Copy string
- `int strcmp(const char *s1, const char *s2)` - Compare strings
- `int strncmp(const char *s1, const char *s2, size_t n)` - Compare strings with limit

### Memory Functions

- `void *memset(void *s, int c, size_t n)` - Fill memory with byte
- `void *memcpy(void *dest, const void *src, size_t n)` - Copy memory

## Usage

```c
#include <string.h>

void example(void) {
    char buffer[64];

    // Copy string
    strcpy(buffer, "hello");

    // Get length
    size_t len = strlen(buffer);  // len = 5

    // Compare strings
    if (strcmp(buffer, "hello") == 0) {
        // Strings are equal
    }

    // Memory operations
    memset(buffer, 0, sizeof(buffer));
    memcpy(buffer, "data", 4);
}
```

## Testing

### Kernel Tests

Run minilibc tests at boot:

```bash
make run KERNEL_CMDLINE='test=minilibc'
```

### Integration Tests

Run Python integration tests:

```bash
python3 tests/minilibc/string_test.py
```

Or via Makefile:

```bash
make test-minilibc
```

## Configuration

Minilibc tests are controlled by the `CONFIG_MINILIBC_TESTS` configuration option in `kernel.config`:

```makefile
# Minilibc tests - Minimal string library tests
# Set to 1 to enable, 0 to disable
CONFIG_MINILIBC_TESTS ?= 1
```

## Implementation Details

### strlen

Iterates through the string until the null terminator is found, counting characters.

```c
size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len] != '\0') {
        len++;
    }
    return len;
}
```

### strcpy

Copies characters from source to destination (including null terminator) and returns the destination pointer.

```c
char *strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++) != '\0') {
        /* Copy until null terminator */
    }
    return dest;
}
```

### strcmp

Compares two strings character by character. Returns the difference of the first mismatching characters.

```c
int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}
```

### strncmp

Similar to strcmp but compares at most `n` characters.

```c
int strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) {
        return 0;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}
```

### memset

Fills a memory region with a specified byte value.

```c
void *memset(void *s, int c, size_t n) {
    unsigned char *p = s;
    while (n-- > 0) {
        *p++ = (unsigned char)c;
    }
    return s;
}
```

### memcpy

Copies `n` bytes from source to destination. Assumes non-overlapping regions.

```c
void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = dest;
    const unsigned char *s = src;
    while (n-- > 0) {
        *d++ = *s++;
    }
    return dest;
}
```

## Test Coverage

The minilibc test suite includes comprehensive edge case testing:

### strlen Tests (5 tests)
- Empty string
- Basic string
- Single character
- String with spaces
- Special characters (newline, tab, control chars)

### strcpy Tests (4 tests)
- Basic copy with return value verification
- Empty source string
- Single character
- String with spaces

### strcmp Tests (7 tests)
- Equal strings
- Both empty strings
- One empty, one non-empty (both directions)
- Less than comparison
- Greater than comparison
- Prefix vs longer string
- Case sensitivity verification

### strncmp Tests (6 tests)
- Full equal strings
- Equal prefix within limit
- Zero limit (should always return 0)
- Both empty strings
- Limit shorter than either string
- One string is prefix of other

### memset Tests (6 tests)
- Basic fill with pattern verification
- Zero length (no-op)
- Zero byte fill
- Return value verification
- Odd size (13 bytes)
- Full byte range (128 bytes)

### memcpy Tests (7 tests)
- Basic copy with return value verification
- Zero length (no-op)
- Single byte copy
- Return value verification
- Exact byte count verification
- Source preservation verification
- Full byte range copy (128 bytes)

**Total: 37 comprehensive tests**

All tests pass successfully, covering edge cases, boundary conditions, and kernel-specific considerations like stack size limitations.

## Future Extensions

Potential additions to minilibc:

- More string functions (strchr, strstr, strcat, etc.)
- Character functions (toupper, tolower, isalpha, etc.)
- Integer conversion (atoi, itoa, etc.)
- More memory functions (memmove, memcmp, etc.)

## Implementation Notes

- All functions use stack allocation only
- No calls to external libc
- Compatible with kernel environment (no syscalls)
- Follows kernel coding conventions (snake_case, Doxygen comments)
- Located in `lib/minilibc/string.c` with header in `include/string.h`
