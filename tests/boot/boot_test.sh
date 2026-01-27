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
KERNEL_ISO="${SCRIPT_DIR}/../jakernel.iso"
QEMU_TIMEOUT=5

# Source test library (KERNEL_ISO must be set before sourcing)
source "${SCRIPT_DIR}/test_lib.sh"

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

    # Run QEMU and capture output
    local output_file=$(run_qemu_capture 1 ${QEMU_TIMEOUT})

    echo "Analyzing boot logs..."
    echo ""

    local boot_log=$(cat "$output_file" 2>/dev/null || echo "")

    # Test 1: Kernel greeting message
    if echo "$boot_log" | grep -q "Hello, JAKernel"; then
        print_result "Kernel greeting message" "true"
    else
        print_result "Kernel greeting message" "false" "No greeting message found"
    fi

    # Test 2: BSP initialization started
    if echo "$boot_log" | grep -q "BSP: Initializing"; then
        print_result "BSP initialization started" "true"
    else
        print_result "BSP initialization started" "false" "BSP init message not found"
    fi

    # Test 3: BSP initialization completed
    if echo "$boot_log" | grep -q "BSP: Initialization complete"; then
        print_result "BSP initialization completed" "true"
    else
        print_result "BSP initialization completed" "false" "BSP completion message not found"
    fi

    # Test 4: CPU 0 (BSP) booted
    if echo "$boot_log" | grep -q "CPU 0.*APIC ID.*Successfully booted"; then
        print_result "CPU 0 (BSP) booted" "true"
    else
        print_result "CPU 0 (BSP) booted" "false" "CPU 0 boot message not found"
    fi

    # Test 5: No exceptions
    assert_no_exceptions "$output_file"

    # Test 6: APIC initialization present
    if echo "$boot_log" | grep -q "\[APIC\]"; then
        print_result "APIC initialization" "true" "APIC messages found"
    else
        print_result "APIC initialization" "false" "No APIC messages found"
    fi

    # Test 7: Timer initialization present
    if echo "$boot_log" | grep -q "\[APIC_TIMER\]"; then
        print_result "APIC timer initialization" "true" "Timer init messages found"
    else
        print_result "APIC timer initialization" "false" "No timer init messages found"
    fi

    print_summary
}

main "$@"
