#!/bin/bash
#
# readonly_visibility_test.sh - Read-Only Visibility Test
#
# This test verifies that the monitor creates read-only mappings
# for nested kernel pages so the outer kernel can inspect but not modify them.
#
# Usage: ./readonly_visibility_test.sh
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
    echo "Read-Only Visibility Test"
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
    local output_file=$(run_make_kernel_cmdline "test=readonly_visibility" ${QEMU_TIMEOUT})

    echo "Verifying read-only visibility test results..."
    echo ""

    # Check for [TEST] PASSED: readonly_visibility marker
    assert_test_passed "readonly_visibility" "$output_file"

    print_summary
}

main "$@"
