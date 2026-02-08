#!/bin/bash
#
# apic_timer_test.sh - APIC Timer Test
#
# This test verifies that the APIC timer works correctly.
# It checks for APIC timer initialization and verifies the timer fires
# and outputs the expected mathematician quotes.
#
# Usage: ./apic_timer_test.sh
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
    echo "APIC Timer Test"
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
    local output_file=$(run_make_kernel_cmdline "test=timer" ${QEMU_TIMEOUT})

    echo "Verifying APIC timer test results..."
    echo ""

    # Check for [TEST] PASSED: timer marker
    assert_test_passed "timer" "$output_file"

    print_summary
}

main "$@"
