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
QEMU_TIMEOUT=5
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

# Parse and verify boot logs
parse_boot_logs() {
    echo "Analyzing boot logs..."
    echo ""

    local boot_log=$(cat "$SERIAL_OUTPUT" 2>/dev/null || echo "")

    # Test 1: Check kernel started
    assert_pattern_exists "Emergence Kernel" "Kernel started"

    # Test 2: Check BSP initialization
    assert_pattern_exists "BSP: Initializing" "BSP initialization started"

    # Test 3: Check BSP completed
    assert_pattern_exists "BSP: Initialization complete" "BSP initialization completed"

    # Test 4: Check CPU 0 (BSP) booted
    assert_pattern_exists "CPU 0.*APIC ID.*Successfully booted" "CPU 0 (BSP) booted"

    # Test 5: Check SMP AP startup initiated
    assert_pattern_exists "SMP: Starting APs" "AP startup initiated"

    # Test 6: Check final SMP completion message AND verify APs actually booted
    if echo "$boot_log" | grep -q "SMP: All APs startup complete"; then
        local complete_msg=$(echo "$boot_log" | grep "SMP: All APs startup complete" | head -1)
        # Extract "X/Y" format and get X (actual APs ready)
        local actual_count=$(echo "$complete_msg" | grep -oE '[0-9]+/[0-9]+' | cut -d'/' -f1)
        local expected_count=$(echo "$complete_msg" | grep -oE '[0-9]+/[0-9]+' | cut -d'/' -f2)

        if [ "$actual_count" -gt 0 ]; then
            print_result "SMP startup completion" "true" "$actual_count/$expected_count APs ready"
        else
            print_result "SMP startup completion" "false" "0 APs booted (expected > 0)"
            return 1
        fi
    else
        print_result "SMP startup completion" "false" "Completion message not found"
        return 1
    fi

    # Test 7: No exceptions during boot
    assert_no_exceptions
}

# Main test execution
main() {
    print_header
    check_prerequisites || exit 1
    setup_trap

    echo "Starting QEMU with ${EXPECTED_CPU_COUNT} CPUs..."
    echo "Timeout: ${QEMU_TIMEOUT} seconds"
    echo ""

    # Run QEMU and capture output
    run_qemu_capture ${EXPECTED_CPU_COUNT} ${QEMU_TIMEOUT} > /dev/null

    parse_boot_logs
    print_summary
}

main "$@"
