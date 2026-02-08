#!/bin/bash
#
# boot_test.sh - Basic Kernel Boot Test
#
# This test verifies that the kernel boots correctly on a single CPU.
# It checks for the basic boot sequence including BSP initialization.
#
# Usage: ./boot_test.sh
#

set -e

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KERNEL_ISO="${SCRIPT_DIR}/../../emergence.iso"
QEMU_TIMEOUT=3

# Source test library (KERNEL_ISO must be set before sourcing)
source "${SCRIPT_DIR}/../lib/test_lib.sh"

# Print test header
print_header() {
    echo "========================================"
    echo "Basic Kernel Boot Test"
    echo "========================================"
    echo "CPU Count: 1"
    echo "ISO: ${KERNEL_ISO}"
    echo ""
}

# Main test execution
main() {
    print_header
    check_prerequisites || exit 1
    setup_trap

    echo "Starting QEMU with 1 CPU..."
    echo "Timeout: ${QEMU_TIMEOUT} seconds"
    echo ""

    # Run QEMU with kernel test command line and capture output
    local output_file=$(run_make_kernel_cmdline "test=boot" ${QEMU_TIMEOUT})

    echo "Verifying boot test results..."
    echo ""

    # Check for [TEST] PASSED: boot marker
    assert_test_passed "boot" "$output_file"

    print_summary
}

main "$@"
