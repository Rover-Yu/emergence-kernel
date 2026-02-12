# Emergence Kernel Changelog

This document tracks significant changes and milestones in the Emergence Kernel development.

## [Unreleased]

## [0.2.1] - 2025-02-12

### Documentation Reorganization

**Moved Project Documentation to docs/:**
- `ARCH_SYSCALL_STATUS.md` - Syscall implementation status
- `CHANGELOG.md` - This changelog (moved to docs/)
- `MEMORY_MAPPINGS.md` - Updated to v1.1 (kernel relocation complete)

**Fixed Directory Naming:**
- Renamed `skills/archiecture/` → `skills/architecture/` (typo fix)

**Updated README.md:**
- Condensed "Recent Developments" to show only latest highlights
- Added reference to docs/CHANGELOG.md for full history

**Skills Enhanced:**
- Added QEMU Launch Guidelines to `skills/build/skill.md`
- "Only use testing framework to launch QEMU" (no direct QEMU commands)
- Fixed `CONFIG_TESTS_NK_TRAMPOLINE` default value in `kernel.config`

## [0.2.0] - 2025-02-08

### Added - Minilibc Implementation

**New Feature: Minimal C Library (minilibc)**

Implemented `lib/minilibc/`, a minimal C library providing essential string and memory manipulation functions without external libc dependencies.

**Implemented Functions:**
- `strlen()` - Calculate string length
- `strcpy()` - Copy string
- `strcmp()` - Compare two strings
- `strncmp()` - Compare strings with limit
- `memset()` - Fill memory with constant byte
- `memcpy()` - Copy memory area

**Test Coverage:**
- 37 comprehensive kernel tests covering edge cases:
  - strlen: 5 tests (empty, basic, single char, spaces, special chars)
  - strcpy: 4 tests (basic, empty src, single char, spaces)
  - strcmp: 7 tests (equal, both empty, one empty, less, greater, prefix, case)
  - strncmp: 6 tests (equal, prefix, zero, both empty, limit, prefix)
  - memset: 6 tests (basic, zero, zero byte, return, odd size, byte range)
  - memcpy: 7 tests (basic, zero, single byte, return, exact count, src unchanged, byte range)
- Python integration test (`tests/minilibc/string_test.py`)
- All tests pass successfully

**Code Quality:**
- Self-contained implementation using only stack memory
- No external libc dependencies
- Kernel-safe (no syscalls or dynamic allocation)
- Follows kernel coding conventions (snake_case, Doxygen comments)
- Located in `lib/minilibc/string.c` with header in `include/string.h`

**Refactoring:**
- Removed duplicate `simple_strcmp` and `simple_strlen` functions from `kernel/test.c`
- Test framework now uses minilibc's `strcmp` function
- Net reduction: 30 lines of code

**Documentation:**
- Added `docs/minilibc.md` with comprehensive documentation
- Updated `docs/ROADMAP.md` with minilibc status

**Files Added:**
- `include/string.h` - Public API header
- `lib/minilibc/string.c` - Implementation
- `tests/minilibc/string_test.py` - Python integration test
- `docs/minilibc.md` - Documentation

**Files Modified:**
- `Makefile` - Added minilibc sources, build rules, test target
- `kernel.config` - Added CONFIG_MINILIBC_TESTS configuration
- `kernel/test.c` - Added minilibc tests, refactored to use strcmp
- `arch/x86_64/main.c` - Added minilibc test invocation
- `tests/lib/assertions.py` - Added assert_not_in_output helper
- `tests/run_all_tests.py` - Added minilibc to test suite
- `docs/ROADMAP.md` - Updated with minilibc status

**Configuration:**
```makefile
CONFIG_MINILIBC_TESTS=1  # Enable minilibc tests (default: enabled)
```

**Usage:**
```c
#include <string.h>

void example(void) {
    char buffer[64];

    strcpy(buffer, "hello");
    size_t len = strlen(buffer);  // len = 5

    if (strcmp(buffer, "hello") == 0) {
        // Strings are equal
    }

    memset(buffer, 0, sizeof(buffer));
    memcpy(buffer, "data", 4);
}
```

**Testing:**
```bash
# Run minilibc tests at boot
make run KERNEL_CMDLINE='test=minilibc'

# Run Python integration test
make test-minilibc

# Run full test suite
make test
```

---

## [0.1.0] - Earlier Releases

### Core Kernel Features
- Multiboot2 boot with GRUB
- Long Mode (64-bit) transition
- Symmetric Multi-Processing (SMP) with AP startup
- Device driver framework (probe/init/remove pattern)
- Local APIC, I/O APIC, interrupt handling, and timers
- Slab allocator for small object allocation
- VGA and serial console output
- Unified test framework with runtime test selection

---

## Version Convention

- **Major version (X.0.0)**: Significant architectural changes or major features
- **Minor version (0.X.0)**: New features or substantial improvements
- **Patch version (0.0.X)**: Bug fixes, small improvements, documentation updates

---

*For detailed information about specific features, see the documentation in `docs/`.*
