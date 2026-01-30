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
QEMU_TIMEOUT=10

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

    # Run QEMU and capture output
    local output_file=$(run_qemu_capture 1 ${QEMU_TIMEOUT})

    echo "Analyzing boot logs for Nested Kernel invariants..."
    echo ""

    local boot_log=$(cat "$output_file" 2>/dev/null || echo "")

    # Verify monitor initialization occurred
    if ! echo "$boot_log" | grep -q "MONITOR: Initializing nested kernel architecture"; then
        print_result "Monitor initialization" "false" "Monitor not initialized"
        print_summary
        exit 1
    fi
    print_result "Monitor initialization" "true"

    # Verify CR3 switch to unprivileged mode occurred
    if ! echo "$boot_log" | grep -q "KERNEL: Page table switch complete"; then
        print_result "CR3 switch to unprivileged mode" "false" "No CR3 switch found"
        print_summary
        exit 1
    fi
    print_result "CR3 switch to unprivileged mode" "true"

    echo ""
    echo "Checking Nested Kernel invariant results:"
    echo ""

    # Count total passes - look for "[CPU X] Nested Kernel invariants: PASS"
    # The new quiet mode format: [CPU 0] Nested Kernel invariants: PASS
    local total_passed=0
    local total_cpus=0

    # Check each CPU's result
    for cpu in 0 1 2 3; do
        if echo "$boot_log" | grep -q "\[CPU $cpu\] Nested Kernel invariants: PASS"; then
            print_result "CPU $cpu invariants" "true"
            total_passed=$((total_passed + 1))
        elif echo "$boot_log" | grep -q "\[CPU $cpu\] Nested Kernel invariants: FAIL"; then
            print_result "CPU $cpu invariants" "false" "Invariant check failed"
        fi
        # Count how many CPUs we saw (either PASS or FAIL)
        if echo "$boot_log" | grep -q "\[CPU $cpu\] Nested Kernel invariants:"; then
            total_cpus=$((total_cpus + 1))
        fi
    done

    echo ""
    echo "Nested Kernel Invariants Summary:"
    echo "  Passed: ${total_passed}/${total_cpus}"

    # Check that we got the expected number of CPUs
    if [ "$total_cpus" -lt 1 ]; then
        echo "  Status: ERROR - No CPU results found"
        print_summary
        exit 1
    fi

    # All CPUs should pass
    if [ "$total_passed" -eq "$total_cpus" ]; then
        echo "  Status: ALL INVARIANTS ENFORCED"
    else
        echo "  Status: SOME INVARIANTS VIOLATED"
    fi

    # Update test counters
    TESTS_PASSED=$total_passed
    TESTS_FAILED=$((total_cpus - total_passed))

    print_summary
}

main "$@"
