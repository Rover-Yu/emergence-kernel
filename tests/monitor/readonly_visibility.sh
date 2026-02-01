#!/bin/bash
#
# readonly_visibility.sh - Read-Only Visibility Test
#
# This test verifies that the monitor creates read-only mappings
# for nested kernel pages so the outer kernel can inspect but not modify them.
#
# Usage: ./readonly_visibility.sh
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

    # Run QEMU and capture output
    local output_file=$(run_qemu_capture 1 ${QEMU_TIMEOUT})

    echo "Analyzing boot logs for read-only visibility..."
    echo ""

    local boot_log=$(cat "$output_file" 2>/dev/null || echo "")
    local all_passed=true

    # Test 1: Verify PCD initialization
    if echo "$boot_log" | grep -q "PCD: Initialized successfully"; then
        print_result "PCD initialization" "true"
    else
        print_result "PCD initialization" "false" "No PCD init message"
        all_passed=false
    fi

    # Test 2: Verify monitor initialization
    if echo "$boot_log" | grep -q "MONITOR: Initializing nested kernel architecture"; then
        print_result "Monitor initialization" "true"
    else
        print_result "Monitor initialization" "false" "Monitor not initialized"
        all_passed=false
    fi

    # Test 3: Verify read-only mappings creation started
    if echo "$boot_log" | grep -q "MONITOR: Creating read-only mappings"; then
        print_result "Read-only mappings creation started" "true"
    else
        print_result "Read-only mappings creation started" "false" "No RO mappings message"
        all_passed=false
    fi

    # Test 4: Verify read-only mappings were created
    if echo "$boot_log" | grep -q "MONITOR: Created.*read-only mappings"; then
        print_result "Read-only mappings created" "true"
    else
        print_result "Read-only mappings created" "false" "No RO mappings count"
        all_passed=false
    fi

    # Test 5: Verify Nested Kernel invariants pass
    if echo "$boot_log" | grep -q "invariants: PASS"; then
        print_result "Nested Kernel invariants" "true"
    else
        print_result "Nested Kernel invariants" "false" "Invariants failed"
        all_passed=false
    fi

    # Test 6: Verify all page tables are marked as NK_PGTABLE
    if echo "$boot_log" | grep -q "MONITOR: Page tables initialized"; then
        print_result "All page tables marked NK_PGTABLE" "true"
    else
        print_result "All page tables marked NK_PGTABLE" "false" "Page table marking incomplete"
        all_passed=false
    fi

    echo ""
    echo "Read-Only Visibility Test Summary:"
    if [ "$all_passed" = true ]; then
        echo "  Status: ALL TESTS PASSED"
        TESTS_PASSED=1
        TESTS_FAILED=0
    else
        echo "  Status: SOME TESTS FAILED"
        TESTS_PASSED=0
        TESTS_FAILED=1
    fi

    print_summary
}

main "$@"
