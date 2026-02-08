#!/bin/bash
#
# slab_test.sh - Slab Allocator Integration Test
#
# This test verifies that the slab allocator works correctly.
# It checks for slab initialization and various allocation tests.
#
# Usage: ./slab_test.sh
#

set -e

# Source test library
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KERNEL_ISO="${SCRIPT_DIR}/../../emergence.iso"
TEST_LIB_DIR="${SCRIPT_DIR}/../lib"

if [ -f "${TEST_LIB_DIR}/test_lib.sh" ]; then
    source "${TEST_LIB_DIR}/test_lib.sh"
else
    echo "Error: test_lib.sh not found at ${TEST_LIB_DIR}"
    exit 1
fi

# Main test execution
main() {
    # Test configuration
    local TEST_NAME="Slab Allocator"
    local TEST_CPUS=2
    local TEST_TIMEOUT=3

    echo "========================================"
    echo "  ${TEST_NAME} Integration Test"
    echo "========================================"
    echo ""

    # Setup cleanup trap
    setup_trap

    # Check prerequisites
    check_prerequisites || exit 1

    # Run kernel with specified CPU count
    echo "Running kernel with ${TEST_CPUS} CPU(s)..."
    echo "Timeout: ${TEST_TIMEOUT} seconds"
    echo ""

    # Run QEMU with kernel test command line and capture output
    local output_file=$(run_make_kernel_cmdline "test=slab" ${TEST_TIMEOUT})

    echo "Verifying slab allocator test results..."
    echo ""

    cat $output_file
    # Check for [TEST] PASSED: slab marker
    assert_test_passed "slab" "$output_file"

    print_summary
}

main "$@"
