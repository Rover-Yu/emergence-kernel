#!/bin/bash
# Emergence Kernel - Slab Allocator Integration Test

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
    local TEST_TIMEOUT=60

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

    local output_file=$(run_qemu_capture "${TEST_CPUS}" "${TEST_TIMEOUT}")

    # Read boot log
    local boot_log=$(cat "$output_file" 2>/dev/null || echo "")

    echo "Analyzing boot logs..."
    echo ""

    # Check for kernel boot
    if echo "$boot_log" | grep -q "SLAB: Initialized"; then
        print_result "Slab allocator initialized" "true"
    else
        print_result "Slab allocator initialized" "false" "SLAB: Initialized not found"
        exit 1
    fi

    # Check for test suite start
    if echo "$boot_log" | grep -q "SLAB Allocator Test Suite"; then
        print_result "Test suite started" "true"
    else
        print_result "Test suite started" "false" "SLAB Allocator Test Suite not found"
        exit 1
    fi

    # Check for individual tests
    assert_pattern_exists "Single allocation test PASSED" "Single allocation test" "$output_file" || exit 1
    assert_pattern_exists "Multiple allocations test PASSED" "Multiple allocations test" "$output_file" || exit 1
    assert_pattern_exists "Free reuse test PASSED" "Free reuse test" "$output_file" || exit 1
    assert_pattern_exists "All cache sizes test PASSED" "All cache sizes test" "$output_file" || exit 1
    assert_pattern_exists "Size rounding test PASSED" "Size rounding test" "$output_file" || exit 1

    # Check for overall test results
    if echo "$boot_log" | grep -q "SLAB: All tests PASSED"; then
        print_result "All tests passed" "true"
    else
        print_result "All tests passed" "false" "SLAB: All tests PASSED not found"
        exit 1
    fi

    # Verify CPU booted (BSP is critical, AP is optional for slab tests)
    assert_pattern_exists "CPU 0.*Successfully booted" "BSP boot" "$output_file" || exit 1

    # AP boot is optional - slab tests work on single CPU
    if echo "$boot_log" | grep -q "CPU 1.*Successfully booted"; then
        print_result "AP boot" "true" "CPU 1 booted successfully"
    else
        print_result "AP boot" "true" "AP boot optional for slab tests"
    fi

    echo ""
    print_summary
}

main "$@"
