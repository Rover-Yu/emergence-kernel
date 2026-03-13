# Emergence Kernel Changelog

This document tracks significant changes and milestones in the Emergence Kernel development.

## [Unreleased]

### Added - Syscall Framework & User Mode Execution (March 2026)

**New Feature: Complete SYSCALL/SYSRET Implementation**

Implemented full system call framework with ring 0 ↔ ring 3 transitions, enabling user mode processes to make kernel requests.

**Implemented Syscalls:**
- `SYS_write` (1) - Write to file descriptor (stdout)
- `SYS_exit` (2) - Terminate current process
- `SYS_yield` (3) - Voluntarily yield CPU to scheduler
- `SYS_getpid` (4) - Get current process ID
- `SYS_fork` (5) - Create child process
- `SYS_wait` (6) - Wait for child process to exit

**Architecture Components:**
- **Process Management** (`kernel/process.c`, `process.h`)
  - Process control block (process_t) with PID tracking
  - Parent-child relationship management
  - Process states: CREATED, RUNNING, ZOMBIE
  - Fork creates new process with separate PID
  - Exit and wait for zombie reaping

- **Memory Isolation** (`arch/x86_64/uaccess.c`, `include/uaccess.h`)
  - `copy_from_user()` safely copies data from user space
  - Pointer validation with exception handling
  - Static kernel buffers to avoid PMM corruption during syscalls

- **Virtual Memory** (`kernel/vm.c`, `vm.h`)
  - Address space management structures
  - Page table manipulation APIs (future: per-process page tables)

- **Thread Context Preservation**
  - Fixed `thread_get_current()` returning NULL from user mode
  - Idle thread association with test process
  - `thread->process` link enables PID tracking

**Critical Bug Fixes:**
- Fixed syscall register mapping to follow System V AMD64 ABI
  - User RAX→RDI(nr), RDI→RSI(a1), RSI→RDX(a2), RDX→RCX(a3)
  - Previously swapped arg2/arg3 due to incorrect mapping
- Fixed int_to_string buffer overflow (wrote to buffer-1)
- Fixed print_number macro clobbering RDX after int_to_string
- Fixed sys_write off-by-one buffer overflow (count > 4095)
- Fixed multiboot command line preference (was preferring embedded)

**Test Framework Enhancements:**
- Fixed regex patterns to match klog format `[CPU<N>][INFO][TEST]`
- Fixed Unicode encoding error in qemu_runner.py (errors='replace')
- Fixed test_run_unified mode checks for TEST_MODE_ALL
- Added auto_run check to prevent tests from shutting down system prematurely

**Test Infrastructure:**
- Added `tests/syscall/` directory with syscall test framework
- `syscall_test.S` - User mode program testing all syscalls
- Fixed .format_buffer section placement (.data instead of .bss)
- All 12 tests pass in `make test`

**Known Limitations:**
- Fork requires register state copying (TODO in process.c)
- Usermode test disabled (auto_run=0) due to system shutdown

**Files Added:**
- `arch/x86_64/include/uaccess.h` - User space access macros
- `arch/x86_64/syscall_test.S` - User mode syscall test (203 lines)
- `arch/x86_64/uaccess.c` - User space access implementation (236 lines)
- `kernel/process.c` - Process management (471 lines)
- `kernel/process.h` - Process structures (228 lines)
- `kernel/vm.c` - Memory management (364 lines)
- `kernel/vm.h` - VM structures (262 lines)
- `tests/syscall/syscall_test.c` - Test wrapper (136 lines)
- `tests/syscall/test_syscall.h` - Test header (20 lines)

**Files Modified:**
- `arch/x86_64/include/syscall.h` - Added syscall prototypes
- `arch/x86_64/main.c` - Removed manual test calls, use test_run_unified()
- `arch/x86_64/multiboot2.c` - Fixed command line preference
- `arch/x86_64/serial.h` - Added serial_driver_init() declaration
- `arch/x86_64/syscall.c` - Implemented 6 syscalls (275 lines)
- `arch/x86_64/syscall_entry.S` - Fixed register mapping (37 lines)
- `kernel.config` - Added CONFIG_TESTS_SYSCALL
- `kernel/test.c` - Fixed test_run_unified mode checks
- `kernel/thread.h` - Added user_stack fields to thread_t
- `lib/minilibc/string.c` - Fixed strncpy null termination
- `tests/Makefile` - Added syscall test targets
- `tests/build.mk` - Added syscall test sources
- `tests/lib/qemu_runner.py` - Fixed Unicode encoding
- `tests/run.py` - Fixed regex patterns, added shutdown detection
- `tests/sched/sched_test.c` - Added power.h include
- `tests/testcases.h` - Added syscall test header

**Configuration:**
```makefile
CONFIG_TESTS_SYSCALL=1  # Enable syscall tests (default: enabled)
```

**Testing:**
```bash
# Run syscall test specifically
make test-syscall

# Run full test suite (includes syscall)
make test
```

**Documentation:**
- Updated `README.md` with syscall test in test section
- Updated `docs/ARCH_SYSCALL_STATUS.md` with full implementation status
- Updated roadmap to mark Basic Kernel Capabilities as "In Progress"

---

## [0.2.1] - 2025-02-12

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
