#!/bin/bash
#
# pcd_test.sh - Page Control Data (PCD) Test
#
# This test verifies that the PCD system is correctly initialized
# and tracking page types as expected.
#
# Usage: ./pcd_test.sh
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
    echo "Page Control Data (PCD) Test"
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
    local output_file=$(run_make_kernel_cmdline "test=pcd" ${QEMU_TIMEOUT})

    echo "Verifying PCD test results..."
    echo ""

    # Check for [TEST] PASSED: pcd marker
    assert_test_passed "pcd" "$output_file"

    print_summary
}

main "$@"
