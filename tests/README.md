# Emergence Kernel Test Suite

This directory contains the test suite for Emergence Kernel, organized by component.

## Directory Structure

```
tests/
├── lib/                    # Test framework library
│   ├── __init__.py         # Package exports
│   ├── config.py           # Configuration dataclasses
│   ├── output.py           # Colored terminal output
│   ├── qemu_runner.py      # QEMU execution with cleanup
│   ├── assertions.py       # Pattern matching assertions
│   └── test_framework.py   # Main framework class
├── boot/                   # Boot integration tests
│   └── boot_test.py        # Basic kernel boot test
├── smp/                    # SMP integration tests
│   └── smp_boot_test.py    # SMP boot test
├── timer/                  # Timer integration tests
│   └── apic_timer_test.py  # APIC timer test
├── slab/                   # Slab allocator integration tests
│   ├── slab_test.c         # Slab allocator test suite (compiled into kernel)
│   └── slab_test.py        # Slab allocator integration test
├── pcd/                    # Page Control Data test
│   └── pcd_test.py         # PCD integration test
├── nested_kernel_invariants/  # Nested Kernel invariants test
│   └── nested_kernel_invariants_test.py  # Invariants verification
├── readonly_visibility/    # Read-only visibility test
│   └── readonly_visibility_test.py  # Read-only mappings test
├── nk_protection/          # Nested Kernel protection test
│   ├── nk_protection_test.c    # Protection test (compiled into kernel)
│   └── nk_protection_test.py   # Protection integration test
├── spinlock/               # Kernel test code
│   └── spinlock_test.c     # Spin lock test suite (compiled into kernel)
├── run_all_tests.py        # Test suite runner
└── README.md               # This file
```

## Test Framework

The test suite uses **Python 3** with a modular framework architecture:

### Core Modules

#### `tests/lib/config.py`
Configuration dataclasses:
- `TestConfig` - Test execution configuration
- `TestResult` - Result of a single test
- `TestSuiteSummary` - Summary of a test suite run

#### `tests/lib/output.py`
Colored terminal output:
- `ANSI` enum - Color codes
- `TerminalOutput` class - Formatted output methods
- `print_progress()` - Progress indicator

#### `tests/lib/qemu_runner.py`
QEMU execution with cleanup:
- `QEMURunner` class - Manages QEMU processes
- Context managers for automatic cleanup
- Signal handlers for graceful shutdown

#### `tests/lib/assertions.py`
Pattern matching assertions:
- `Assertions` class - Output verification methods
- `AssertionFailure` exception - Assertion failures

#### `tests/lib/test_framework.py`
Main framework class:
- `TestFramework` class - Orchestrates test execution
- `create_framework()` - Helper for framework creation
- `run_single_test()` - Convenience function

### Resource Management

The framework uses Python context managers to ensure cleanup:

```python
with create_framework(test_name="boot") as framework:
    framework.run_test("boot")
# Cleanup happens automatically here
```

Temporary files are tracked and removed unless `--keep-output` is specified.

### Signal Handling

SIGINT and SIGTERM are caught to ensure cleanup:

```python
def cleanup_handler(signum, frame):
    runner.cleanup_all()
    sys.exit(1)

signal.signal(signal.SIGINT, cleanup_handler)
signal.signal(signal.SIGTERM, cleanup_handler)
```

## Test Categories

### Integration Tests

Integration tests run the kernel in QEMU and analyze the boot logs to verify behavior.

#### `boot/boot_test.py` - Basic Kernel Boot Test
Verifies that the kernel boots correctly on a single CPU. Checks:
- BSP initialization
- APIC initialization
- APIC timer initialization
- CPU 0 (BSP) booted successfully
- No exceptions during boot

**CPUs:** 1 | **Timeout:** 3s

#### `smp/smp_boot_test.py` - SMP Boot Test
Verifies symmetric multi-processor startup. Checks:
- BSP initialization
- AP startup initiation
- All CPUs boot successfully
- No exceptions during SMP boot

**CPUs:** 2 (default) | **Timeout:** 5s

#### `timer/apic_timer_test.py` - APIC Timer Test
Verifies APIC timer functionality. Checks:
- APIC timer initialization
- Timer interrupt firing
- Mathematician quotes output
- Debug character output

**CPUs:** 1 | **Timeout:** 5s

#### `slab/slab_test.py` - Slab Allocator Test
Verifies slab allocator functionality for small object allocation. Checks:
- Slab initialization with 8 power-of-two caches (32B - 4KB)
- Single allocation and free
- Multiple allocations (16 objects from 128B cache)
- Free reuse verification
- All cache sizes (32, 64, 128, 256, 512, 1024, 2048, 4096 bytes)
- Size rounding (requests round to next power-of-two)

**CPUs:** 2 | **Timeout:** 5s

**Note:** Requires `CONFIG_SLAB_TESTS=1` to enable the kernel-compiled test suite.

Kernel tests (`slab_test.c`) verify:
- Single alloc/free operations
- Multiple sequential allocations
- Freed objects are reused
- All 8 cache sizes work correctly
- Size rounding behavior (e.g., 50B → 64B, 1000B → 1024B)

### Monitor/Nested Kernel Tests

Tests for the monitor architecture and nested kernel isolation features.

#### `pcd/pcd_test.py` - Page Control Data Test
Verifies the Page Control Data (PCD) system which tracks page types and ownership.
Checks:
- PCD initialization
- Page type registration
- Statistics display (if enabled)

**CPUs:** 1 | **Timeout:** 3s

#### `nested_kernel_invariants/nested_kernel_invariants_test.py` - Nested Kernel Invariants Test
Verifies that all 6 nested kernel invariants are enforced on both BSP and APs.
Checks:
- Invariant verification on all CPUs
- All invariants pass
- Monitor initialization

**CPUs:** 1 | **Timeout:** 3s

#### `readonly_visibility/readonly_visibility_test.py` - Read-Only Visibility Test
Verifies that the monitor creates read-only mappings for nested kernel pages so the outer kernel can inspect but not modify them.
Checks:
- PCD initialization
- Monitor initialization
- Read-only mappings creation
- Nested Kernel invariants pass
- All page tables marked NK_PGTABLE

**CPUs:** 1 | **Timeout:** 3s

#### `nk_protection/nk_protection_test.py` - Nested Kernel Mappings Protection Test
**Note: This test requires `CONFIG_NK_PROTECTION_TESTS=1` to be enabled and is NOT included in the default test suite.**

This test intentionally triggers page faults to verify that nested kernel mappings are properly protected.
Checks:
- NK protection test initiated
- Running in unprivileged mode
- Page table write attempt
- Page fault triggered

To enable and run:
```bash
make CONFIG_NK_PROTECTION_TESTS=1
make test-nk-protection
```

**Important:** This test will cause the kernel to trigger intentional page faults and shutdown. This is expected behavior - the test verifies that write protection is working by attempting to write to protected page tables.

### Kernel Tests

#### `spinlock/spinlock_test.c`
Comprehensive spin lock and read-write lock test suite, compiled into the kernel.
Tests:
- Single-CPU lock/unlock
- Multi-CPU lock contention
- Read-write lock behavior
- Barrier synchronization

## Running Tests

### Run All Tests

```bash
# Using Python
python3 tests/run_all_tests.py

# Using Makefile
make test
# or
make test-all
```

### Run Individual Tests

```bash
# Using Python directly
python3 tests/boot/boot_test.py
python3 tests/slab/slab_test.py --verbose

# Using Makefile
make test-boot          # Basic kernel boot test (1 CPU)
make test-smp           # SMP boot test (2 CPUs)
make test-apic-timer    # APIC timer test (1 CPU)
make test-slab          # Slab allocator test (2 CPUs)
make test-pcd           # PCD test
make test-nested-kernel # Nested Kernel invariants test
make test-readonly-visibility  # Read-Only visibility test
make test-nk-protection # NK protection test (requires CONFIG_NK_PROTECTION_TESTS=1)
```

### Run Specific Test from Suite

```bash
python3 tests/run_all_tests.py --test boot
python3 tests/run_all_tests.py --test slab --verbose
```

### List Available Tests

```bash
python3 tests/run_all_tests.py --list
```

## Command Line Options

All test scripts support:

| Option | Description |
|--------|-------------|
| `-v, --verbose` | Show detailed test output |
| `--keep-output` | Keep test output files for debugging |
| `--timeout SECONDS` | Set QEMU timeout |
| `--cpus COUNT` | Set CPU count (SMP test) |
| `--help` | Show help message |

The suite runner additionally supports:

| Option | Description |
|--------|-------------|
| `--list` | List available tests |
| `--test NAME` | Run only the specified test |

## Output Format

```
========================================
Basic Kernel Boot Test
========================================
CPU Count: 1
Timeout: 3 seconds

Starting QEMU with 1 CPU...

Running test: boot (1 CPU(s), timeout: 3s)

[PASS] boot test passed

========================================
Test Summary
========================================
Total Tests: 1
Passed: 1
Failed: 0

ALL TESTS PASSED!
========================================
```

## Build Tests

Kernel tests like `spinlock_test.c`, `slab_test.c`, and `nk_protection_test.c` are conditionally compiled into the kernel based on configuration options.

To rebuild with tests enabled:

```bash
# Enable spinlock tests
make CONFIG_SPINLOCK_TESTS=1

# Enable slab tests
make CONFIG_SLAB_TESTS=1

# Enable NK protection tests
make CONFIG_NK_PROTECTION_TESTS=1
```

## Adding New Tests

1. Create a new directory under `tests/` for the component
2. Write the test script using the framework:

```python
#!/usr/bin/env python3
"""
Test Description
"""

import sys
from pathlib import Path

# Add lib directory to path for imports
lib_path = Path(__file__).parent.parent / "lib"
sys.path.insert(0, str(lib_path))

from test_framework import create_framework
from output import TerminalOutput

def main():
    output = TerminalOutput()
    output.print_header("Test Name", width=40)

    framework = create_framework(
        test_name="test_name",
        cpu_count=1,
        timeout=3,
        verbose=False
    )

    if not framework.check_prerequisites():
        sys.exit(1)

    if framework.run_test("test_name", cpu_count=1):
        exit_code = framework.print_summary()
    else:
        exit_code = 1

    sys.exit(exit_code)

if __name__ == "__main__":
    main()
```

3. Add the test to `run_all_tests.py` TESTS list
4. Add a Makefile target in the root Makefile

## Verification

To verify the Python framework works correctly:

```bash
# List tests
python3 tests/run_all_tests.py --list

# Test individual scripts
python3 tests/boot/boot_test.py --help
python3 tests/slab/slab_test.py --help

# Run via Makefile
make test-boot
```
