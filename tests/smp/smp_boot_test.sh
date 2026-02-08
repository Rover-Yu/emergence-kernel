#!/bin/bash
#
# smp_boot_test.sh - Test SMP startup by analyzing QEMU boot logs
#
# This test runs QEMU with a specified CPU count, captures serial output,
# and verifies that all CPUs (BSP + APs) successfully boot.
#
# Usage: ./smp_boot_test.sh [cpu_count]
#   cpu_count: Number of CPUs to test (default: 2)
#

set -e

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KERNEL_ISO="${SCRIPT_DIR}/../../emergence.iso"
QEMU_TIMEOUT=3
EXPECTED_CPU_COUNT=${1:-2}

# Source test library (KERNEL_ISO must be set before sourcing)
source "${SCRIPT_DIR}/../lib/test_lib.sh"

# Print test header
print_header() {
    echo "========================================"
    echo "SMP Boot Test - QEMU Log Analysis"
    echo "========================================"
    echo "CPU Count: ${EXPECTED_CPU_COUNT}"
    echo "ISO: ${KERNEL_ISO}"
    echo ""
}

# Main test execution
main() {
    print_header
    check_prerequisites || exit 1
    setup_trap

    echo "Starting QEMU with ${EXPECTED_CPU_COUNT} CPUs..."
    echo "Timeout: ${QEMU_TIMEOUT} seconds"
    echo ""

    # Run QEMU with kernel test command line and capture output
    # Note: SMP test needs 2 CPUs
    local output_file=$(run_make_kernel_cmdline "test=smp" ${QEMU_TIMEOUT})

    echo "Verifying SMP boot test results..."
    echo ""

    # Check for [TEST] PASSED: smp marker
    assert_test_passed "smp" "$output_file"

    print_summary
}

main "$@"
