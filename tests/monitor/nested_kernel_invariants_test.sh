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

# Check for a specific invariant pass/fail in output
# The output format has INV header on one line, result on following lines:
#   VERIFY: [Inv 1] Description:
#   VERIFY:   details - PASS
#
# For Invariant 6, the format spans 3 lines:
#   VERIFY: [Inv 6] CR3 loaded with pre-declared PTP:
#   VERIFY:   Current CR3: 0x...
#   VERIFY:   monitor_pml4_phys: 0x..., unpriv_pml4_phys: 0x... - PASS
check_invariant() {
    local inv_num="$1"
    local inv_name="$2"
    local output="$3"

    # Get the verification output AFTER "Page table switch complete"
    # This ensures we check the final state where all invariants should pass
    # We need to find the LAST verification output (after unprivileged switch)
    local final_verification=$(echo "$output" | sed -n '/Page table switch complete/,$p' | tail -n +2)

    # Check if the invariant line exists
    if ! echo "$final_verification" | grep -q "VERIFY: \[Inv ${inv_num}\]"; then
        print_result "Invariant ${inv_num}: ${inv_name}" "false" "No verification output found"
        return 1
    fi

    # For Invariant 6, need to check line 3 after header (A 2 instead of A 1)
    # For other invariants, check line 2 after header (A 1)
    local lines_after=1
    if [ "$inv_num" = "6" ]; then
        lines_after=2
    fi

    # Check the lines AFTER the INV header for PASS or FAIL
    local result=$(echo "$final_verification" | grep -A ${lines_after} "VERIFY: \[Inv ${inv_num}\]" | tail -1)

    if echo "$result" | grep -q "PASS"; then
        print_result "Invariant ${inv_num}: ${inv_name}" "true"
        return 0
    else
        print_result "Invariant ${inv_num}: ${inv_name}" "false" "Invariant check shows FAIL or missing result"
        return 1
    fi
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
    echo "Checking all 6 Nested Kernel invariants:"
    echo ""

    # Count total passes
    local total_passed=0
    local total_tests=6

    # Invariant 1: PTPs read-only in outer kernel
    if check_invariant 1 "PTPs read-only in outer kernel" "$boot_log"; then
        total_passed=$((total_passed + 1))
    fi

    # Invariant 2: CR0.WP enforcement active
    if check_invariant 2 "CR0.WP enforcement active" "$boot_log"; then
        total_passed=$((total_passed + 1))
    fi

    # Invariant 3: Global mappings accessible in both views
    if check_invariant 3 "Global mappings accessible in both views" "$boot_log"; then
        total_passed=$((total_passed + 1))
    fi

    # Invariant 4: Context switch mechanism
    if check_invariant 4 "Context switch mechanism" "$boot_log"; then
        total_passed=$((total_passed + 1))
    fi

    # Invariant 5: PTPs writable in nested kernel
    if check_invariant 5 "PTPs writable in nested kernel" "$boot_log"; then
        total_passed=$((total_passed + 1))
    fi

    # Invariant 6: CR3 loaded with pre-declared PTP
    if check_invariant 6 "CR3 loaded with pre-declared PTP" "$boot_log"; then
        total_passed=$((total_passed + 1))
    fi

    echo ""
    echo "Nested Kernel Invariants Summary:"
    echo "  Passed: ${total_passed}/${total_tests}"

    # Check final verdict (from final verification after unprivileged switch)
    local final_verification=$(echo "$boot_log" | sed -n '/Page table switch complete/,$p' | tail -n +2)
    if echo "$final_verification" | grep -q "VERIFY: PASS - All 6 Nested Kernel invariants enforced"; then
        echo "  Status: ALL INVARIANTS ENFORCED"
    elif echo "$final_verification" | grep -q "VERIFY: FAIL - Some invariants violated"; then
        echo "  Status: SOME INVARIANTS VIOLATED"
    else
        echo "  Status: VERDICT UNKNOWN"
    fi

    # Update test counters
    TESTS_PASSED=$total_passed
    TESTS_FAILED=$((total_tests - total_passed))

    print_summary
}

main "$@"
