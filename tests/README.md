# Emergence Kernel Test Suite

This directory contains the test suite for Emergence Kernel, organized by component.

## Directory Structure

```
tests/
├── lib/                    # Test framework library
│   └── test_lib.sh         # Common test utilities
├── boot/                   # Boot integration tests
│   └── boot_test.sh        # Basic kernel boot test
├── smp/                    # SMP integration tests
│   └── smp_boot_test.sh    # SMP boot test
├── timer/                  # Timer integration tests
│   └── apic_timer_test.sh  # APIC timer test
├── spinlock/               # Kernel test code
│   └── spinlock_test.c     # Spin lock test suite (compiled into kernel)
├── run_all_tests.sh        # Test suite runner
└── README.md               # This file
```

## Test Categories

### Integration Tests

Integration tests run the kernel in QEMU and analyze the boot logs to verify behavior.

#### `boot/boot_test.sh` - Basic Kernel Boot Test
Verifies that the kernel boots correctly on a single CPU. Checks:
- BSP initialization
- APIC initialization
- APIC timer initialization
- CPU 0 (BSP) booted successfully
- No exceptions during boot

#### `smp/smp_boot_test.sh` - SMP Boot Test
Verifies symmetric multi-processor startup. Checks:
- BSP initialization
- AP startup initiation
- All CPUs boot successfully
- No exceptions during SMP boot

Usage: `./smp/smp_boot_test.sh [cpu_count]` (default: 2 CPUs)

#### `timer/apic_timer_test.sh` - APIC Timer Test
Verifies APIC timer functionality. Checks:
- APIC timer initialization
- Timer interrupt firing
- Mathematician quotes output
- Debug character output

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
make test
# or
make test-all
# or
cd tests && ./run_all_tests.sh
```

### Run Individual Tests

```bash
# Boot test (1 CPU)
make test-boot
# or
cd tests && ./boot/boot_test.sh

# SMP boot test (2 CPUs)
make test-smp
# or
cd tests && ./smp/smp_boot_test.sh 2

# APIC timer test (1 CPU)
make test-apic-timer
# or
cd tests && ./timer/apic_timer_test.sh
```

### Build Tests

Kernel tests like `spinlock_test.c` are compiled into the kernel by default.
To rebuild with tests:

```bash
make clean && make
```

## Test Framework

The test library `lib/test_lib.sh` provides common utilities:

- `check_prerequisites()` - Verify kernel ISO and QEMU are available
- `run_qemu_capture()` - Run QEMU and capture serial output
- `print_result()` - Print test pass/fail with color
- `print_summary()` - Print test summary with totals
- `assert_no_exceptions()` - Check for exceptions in output
- `assert_debug_char_count()` - Verify debug character counts

## Adding New Tests

1. Create a new directory under `tests/` for the component
2. Write the test script, sourcing `../lib/test_lib.sh`
3. Add the test to `run_all_tests.sh` TESTS array
4. Optionally add a Makefile target

Example test script structure:

```bash
#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KERNEL_ISO="${SCRIPT_DIR}/../../emergence.iso"
source "${SCRIPT_DIR}/../lib/test_lib.sh"

main() {
    check_prerequisites || exit 1
    setup_trap

    # Run QEMU and capture output
    local output_file=$(run_qemu_capture 1 5)

    # Run your checks...
    print_result "Test name" "true/false"

    print_summary
}

main "$@"
```
