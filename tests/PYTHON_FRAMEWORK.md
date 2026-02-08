# Python Test Framework Implementation

This document describes the Python 3 test framework implementation that replaces the bash test infrastructure.

## Architecture

### Core Modules

#### `tests/lib/__init__.py`
Package initialization that exports all main classes.

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

## Test Scripts

All test scripts follow this pattern:

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
    args = parse_arguments()

    output = TerminalOutput()
    output.print_header("Test Name", width=40)

    framework = create_framework(
        test_name="test_name",
        cpu_count=1,
        timeout=3,
        verbose=args.verbose,
        keep_output=args.keep_output
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

## Available Tests

| Test Script | Test Name | CPUs | Timeout |
|-------------|-----------|------|---------|
| `boot/boot_test.py` | boot | 1 | 3s |
| `pcd/pcd_test.py` | pcd | 1 | 3s |
| `slab/slab_test.py` | slab | 2 | 5s |
| `nested_kernel_invariants/nested_kernel_invariants_test.py` | nested_kernel_invariants | 1 | 3s |
| `readonly_visibility/readonly_visibility_test.py` | readonly_visibility | 1 | 3s |
| `timer/apic_timer_test.py` | timer | 1 | 5s |
| `smp/smp_boot_test.py` | smp_boot | 2 | 5s |

## Usage

### Run Individual Tests

```bash
# Using Python directly
python3 tests/boot/boot_test.py
python3 tests/slab/slab_test.py --verbose

# Using Makefile (hybrid mode - uses Python if available)
make test-boot
make test-slab
```

### Run Test Suite

```bash
# Using Python directly
python3 tests/run_all_tests.py
python3 tests/run_all_tests.py --verbose --keep-output

# Using Makefile
make test-all
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

## Makefile Integration

The Makefile uses a hybrid approach:

```makefile
test-boot:
	@$(MAKE) all
	@echo "Running Basic Kernel Boot Test..."
	@if [ -f "tests/boot/boot_test.py" ]; then \
		python3 tests/boot/boot_test.py; \
	else \
		cd tests && ./boot/boot_test.sh; \
	fi
```

This allows gradual migration - Python scripts are used if available, otherwise bash scripts are used as fallback.

## Command Line Options

All test scripts support:

- `-v, --verbose` - Show detailed test output
- `--keep-output` - Keep test output files for debugging
- `--timeout SECONDS` - Set QEMU timeout
- `--cpus COUNT` - Set CPU count (SMP test)
- `--help` - Show help message

The suite runner additionally supports:

- `--list` - List available tests
- `--test NAME` - Run only the specified test

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

## Resource Management

The framework uses Python context managers to ensure cleanup:

```python
with create_framework(test_name="boot") as framework:
    framework.run_test("boot")
# Cleanup happens automatically here
```

Temporary files are tracked and removed unless `--keep-output` is specified.

## Signal Handling

SIGINT and SIGTERM are caught to ensure cleanup:

```python
def cleanup_handler(signum, frame):
    runner.cleanup_all()
    sys.exit(1)

signal.signal(signal.SIGINT, cleanup_handler)
signal.signal(signal.SIGTERM, cleanup_handler)
```

## Verification

To verify the Python framework works correctly:

```bash
# List tests
python3 tests/run_all_tests.py --list

# Test individual scripts
python3 tests/boot/boot_test.py --help
python3 tests/slab/slab_test.py --help

# Run via Makefile (hybrid mode)
make test-boot
```
