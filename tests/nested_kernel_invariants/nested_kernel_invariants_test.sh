#!/bin/bash
#
# nested_kernel_invariants_test.sh - Nested Kernel Invariants Test
#
# This test verifies that all 6 Nested Kernel invariants from the
# ASPLOS '15 paper are correctly enforced:
#
#   Inv 1: PTPs read-only in outer kernel (PTE writable=0)
#   Inv 2: CR0.WP enforcement active (bit 16)
#   Inv 3: Global mappings accessible in both views
#   Inv 4: Context switch mechanism available
#   Inv 5: PTPs writable in nested kernel
#   Inv 6: CR3 loaded with pre-declared PTP
#
# Usage: ./nested_kernel_invariants_test.sh
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
    echo "Nested Kernel Invariants Test"
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
    local output_file=$(run_make_kernel_cmdline "test=nested_kernel_invariants" ${QEMU_TIMEOUT})

    echo "Verifying Nested Kernel invariants test results..."
    echo ""

    # Check for [TEST] PASSED: nested_kernel_invariants marker
    assert_test_passed "nested_kernel_invariants" "$output_file"

    print_summary
}

main "$@"
