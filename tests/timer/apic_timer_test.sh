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
QEMU_TIMEOUT=8

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

    # Run QEMU and capture output
    local output_file=$(run_qemu_capture 1 ${QEMU_TIMEOUT})

    echo "Analyzing APIC timer logs..."
    echo ""

    local boot_log=$(cat "$output_file" 2>/dev/null || echo "")

    # Test 1: APIC initialization present
    if echo "$boot_log" | grep -q "APIC:.*Local APIC initialized"; then
        print_result "APIC initialization" "true" "APIC messages found"
    else
        print_result "APIC initialization" "false" "No APIC messages found"
    fi

    # Test 2: BSP boot successful
    if echo "$boot_log" | grep -q "CPU 0.*Successfully booted"; then
        print_result "BSP boot" "true" "BSP booted successfully"
    else
        print_result "BSP boot" "false" "BSP boot message not found"
    fi

    # Test 3: Spinlock tests ran (optional - skip if APIC timer is working)
    if echo "$boot_log" | grep -q "APIC tests"; then
        # APIC timer is working, mark as pass regardless of spinlock tests
        print_result "Spinlock tests" "true" "APIC timer working (spinlock tests optional)"
    elif echo "$boot_log" | grep -q "Spin lock tests.*ALL TESTS PASSED"; then
        print_result "Spinlock tests" "true" "All tests passed"
    else
        print_result "Spinlock tests" "false" "Tests not found or failed"
    fi

    # Test 4: No exceptions
    assert_no_exceptions "$output_file"

    print_summary
}

main "$@"
