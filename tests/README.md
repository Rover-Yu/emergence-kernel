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
├── monitor/                # Monitor/nested kernel tests
│   ├── pcd_test.sh         # Page Control Data test
│   ├── nested_kernel_invariants_test.sh  # Invariants verification
│   ├── nk_protection_test.sh            # Mappings protection test
│   └── readonly_visibility.sh           # Read-only visibility test
├── nested_kernel_mapping_protection/    # Kernel-compiled tests
│   └── nk_protection_test.c             # Protection test (compiled into kernel)
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

### Monitor/Nested Kernel Tests

Tests for the monitor architecture and nested kernel isolation features.

#### `monitor/pcd_test.sh` - Page Control Data Test
Verifies the Page Control Data (PCD) system which tracks page types and ownership.
Checks:
- PCD initialization
- Page type registration
- Statistics display (if enabled)

#### `monitor/nested_kernel_invariants_test.sh` - Nested Kernel Invariants Test
Verifies that all 6 nested kernel invariants are enforced on both BSP and APs.
Checks:
- Invariant verification on all CPUs
- All invariants pass
- Monitor initialization

#### `monitor/nk_protection_test.sh` - Nested Kernel Mappings Protection Test
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

#### `monitor/readonly_visibility.sh` - Read-Only Visibility Test
Verifies that the monitor creates read-only mappings for nested kernel pages so the outer kernel can inspect but not modify them.
Checks:
- PCD initialization
- Monitor initialization
- Read-only mappings creation
- Nested Kernel invariants pass
- All page tables marked NK_PGTABLE

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

# PCD test
make test-pcd
# or
cd tests && ./monitor/pcd_test.sh

# Nested Kernel invariants test
make test-nested-kernel
# or
cd tests && ./monitor/nested_kernel_invariants_test.sh

# Nested Kernel mappings protection test (requires CONFIG_NK_PROTECTION_TESTS=1)
make test-nk-protection
# or
cd tests && ./monitor/nk_protection_test.sh

# Read-Only visibility test
make test-readonly-visibility
# or
cd tests && ./monitor/readonly_visibility.sh
```

### Build Tests

Kernel tests like `spinlock_test.c` and `nk_protection_test.c` are conditionally compiled into the kernel based on configuration options.

To rebuild with tests enabled:

```bash
# Enable spinlock tests
make CONFIG_SPINLOCK_TESTS=1

# Enable NK protection tests
make CONFIG_NK_PROTECTION_TESTS=1
```

## Test Framework

The test library `lib/test_lib.sh` provides common utilities:

- `check_prerequisites()` - Verify kernel ISO and QEMU are available
- `run_qemu_capture()` - Run QEMU and capture serial output
- `print_result()` - Print test pass/fail with color
- `print_summary()` - Print test summary with totals
- `assert_pattern_exists()` - Check if pattern exists in output
- `assert_pattern_count()` - Check pattern count matches expected
- `assert_no_exceptions()` - Check for exceptions in output
- `assert_debug_char_count()` - Verify debug character counts
- `cleanup()` - Remove temporary files
- `setup_trap()` - Set up cleanup trap

## Adding New Tests

1. Create a new directory under `tests/` for the component
2. Write the test script, sourcing `../lib/test_lib.sh`
3. Add the test to `run_all_tests.sh` TESTS array
4. Add a Makefile target in the root Makefile

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
    assert_pattern_exists "pattern" "Test description"

    print_summary
}

main "$@"
```
