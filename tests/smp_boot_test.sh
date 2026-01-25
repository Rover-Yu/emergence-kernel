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
KERNEL_ISO="${SCRIPT_DIR}/../jakernel.iso"
QEMU_TIMEOUT=5
SERIAL_OUTPUT="/tmp/jakernel_smp_test_$$"
EXPECTED_CPU_COUNT=${1:-2}

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Test counters
TESTS_PASSED=0
TESTS_FAILED=0

# Print test header
print_header() {
    echo "========================================"
    echo "SMP Boot Test - QEMU Log Analysis"
    echo "========================================"
    echo "CPU Count: ${EXPECTED_CPU_COUNT}"
    echo "ISO: ${KERNEL_ISO}"
    echo ""
}

# Print test result
print_result() {
    local test_name="$1"
    local passed="$2"
    local details="$3"

    if [ "$passed" = "true" ]; then
        echo -e "[${GREEN}PASS${NC}] $test_name"
        TESTS_PASSED=$((TESTS_PASSED + 1))
    else
        echo -e "[${RED}FAIL${NC}] $test_name"
        TESTS_FAILED=$((TESTS_FAILED + 1))
    fi

    if [ -n "$details" ]; then
        echo "      $details"
    fi
}

# Cleanup on exit
cleanup() {
    if [ -f "$SERIAL_OUTPUT" ]; then
        rm -f "$SERIAL_OUTPUT"
    fi
}

trap cleanup EXIT

# Check prerequisites
check_prerequisites() {
    if [ ! -f "$KERNEL_ISO" ]; then
        echo -e "${RED}Error: Kernel ISO not found: $KERNEL_ISO${NC}"
        echo "Please run 'make' first to build the kernel."
        exit 1
    fi

    if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
        echo -e "${RED}Error: qemu-system-x86_64 not found${NC}"
        echo "Please install QEMU."
        exit 1
    fi
}

# Run QEMU and capture serial output
run_qemu() {
    echo "Starting QEMU with ${EXPECTED_CPU_COUNT} CPUs..."
    echo "Timeout: ${QEMU_TIMEOUT} seconds"
    echo ""

    # Run QEMU with serial output to file, timeout after specified seconds
    # Include shutdown device for clean VM exit after SMP startup completes (8-bit I/O)
    timeout ${QEMU_TIMEOUT} qemu-system-x86_64 \
        -M pc \
        -m 128M \
        -nographic \
        -monitor none \
        -smp ${EXPECTED_CPU_COUNT} \
        -cdrom "${KERNEL_ISO}" \
        -device isa-debug-exit,iobase=0xB004,iosize=1 \
        -serial file:"${SERIAL_OUTPUT}" >/dev/null 2>&1 || true

    if [ ! -f "$SERIAL_OUTPUT" ]; then
        echo -e "${RED}Error: Failed to capture serial output${NC}"
        exit 1
    fi
}

# Parse and verify boot logs
parse_boot_logs() {
    echo "Analyzing boot logs..."
    echo ""

    local boot_log=$(cat "$SERIAL_OUTPUT" 2>/dev/null || echo "")

    # Test 1: Check kernel started
    if echo "$boot_log" | grep -q "Hello, JAKernel"; then
        print_result "Kernel started" "true"
    else
        print_result "Kernel started" "false" "No kernel greeting found"
    fi

    # Test 2: Check BSP initialization
    if echo "$boot_log" | grep -q "BSP: Initializing"; then
        print_result "BSP initialization started" "true"
    else
        print_result "BSP initialization started" "false" "BSP init message not found"
    fi

    # Test 3: Check BSP completed
    if echo "$boot_log" | grep -q "BSP: Initialization complete"; then
        print_result "BSP initialization completed" "true"
    else
        print_result "BSP initialization completed" "false" "BSP completion message not found"
    fi

    # Test 4: Check CPU 0 (BSP) booted
    if echo "$boot_log" | grep -q "CPU 0.*APIC ID.*Successfully booted"; then
        print_result "CPU 0 (BSP) booted" "true"
    else
        print_result "CPU 0 (BSP) booted" "false" "CPU 0 boot message not found"
    fi

    # Test 5: Check SMP AP startup initiated
    if echo "$boot_log" | grep -q "SMP: Starting APs"; then
        print_result "AP startup initiated" "true"
    else
        print_result "AP startup initiated" "false" "AP startup message not found"
    fi

    # Test 6: Check for AP startup messages
    local ap_count=0
    for i in $(seq 1 $((EXPECTED_CPU_COUNT - 1))); do
        if echo "$boot_log" | grep -q "SMP: Starting AP $i"; then
            ap_count=$((ap_count + 1))
        fi
    done

    local expected_aps=$((EXPECTED_CPU_COUNT - 1))
    if [ "$ap_count" -eq "$expected_aps" ]; then
        print_result "AP startup messages" "true" "$ap_count APs started"
    elif [ "$ap_count" -gt 0 ]; then
        print_result "AP startup messages" "false" "Expected $expected_aps APs, found $ap_count"
    else
        print_result "AP startup messages" "false" "No AP startup messages found"
    fi

    # Test 7: Check for AP initialization success messages
    # Note: Serial output may have corruption due to concurrent writes from multiple CPUs
    # so we count all "[AP] CPU X initialized successfully" messages regardless of exact number
    local ready_aps=$(echo "$boot_log" | grep -c "\[AP\] CPU.*initialized successfully" || echo "0")

    if [ "$ready_aps" -eq "$expected_aps" ]; then
        print_result "APs initialized" "true" "$ready_aps/$expected_aps APs ready"
    elif [ "$ready_aps" -gt 0 ]; then
        print_result "APs initialized" "false" "Expected $expected_aps ready APs, found $ready_aps"
    else
        print_result "APs initialized" "false" "No AP ready messages found"
    fi

    # Test 8: Check final SMP completion message
    if echo "$boot_log" | grep -q "SMP: All APs startup complete"; then
        # Extract the actual count from the log
        local complete_msg=$(echo "$boot_log" | grep "SMP: All APs startup complete" | head -1)
        local complete_count=$(echo "$complete_msg" | grep -oE '[0-9]+/[0-9]+' | head -1)
        print_result "SMP startup completion" "true" "$complete_count APs ready"
    else
        print_result "SMP startup completion" "false" "Completion message not found"
    fi

    # Test 9: Check AP trampoline debug characters (advanced check)
    # The AP trampoline outputs specific debug chars: HG3APLXDSQALILTCWC
    if echo "$boot_log" | grep -q "HG3APLXDSQALILTCWC"; then
        print_result "AP trampoline execution" "true" "Debug sequence found"
    else
        # Check for at least some trampoline characters
        if echo "$boot_log" | grep -qE "HG3A|PLXD|SQAL"; then
            print_result "AP trampoline execution" "true" "Partial debug sequence found"
        else
            print_result "AP trampoline execution" "false" "Trampoline debug chars not found"
        fi
    fi

    # Test 10: Verify CPU count in logs
    # BSP uses: "CPU 0 (APIC ID 0): Successfully booted"
    # APs use: "[AP] CPU X initialized successfully"
    local bsp_count=$(echo "$boot_log" | grep -c "CPU.*APIC ID.*Successfully booted" || echo "0")
    local ap_count=$(echo "$boot_log" | grep -c "\[AP\] CPU.*initialized successfully" || echo "0")
    local logged_cpu_count=$((bsp_count + ap_count))
    if [ "$logged_cpu_count" -eq "$EXPECTED_CPU_COUNT" ]; then
        print_result "Expected CPU count" "true" "$logged_cpu_count CPUs logged (BSP: $bsp_count, APs: $ap_count)"
    else
        print_result "Expected CPU count" "false" "Expected $EXPECTED_CPU_COUNT CPUs, found $logged_cpu_count (BSP: $bsp_count, APs: $ap_count)"
    fi
}

# Print summary
print_summary() {
    local total_tests=$((TESTS_PASSED + TESTS_FAILED))
    echo ""
    echo "========================================"
    echo "Test Summary"
    echo "========================================"
    echo "Total Tests: $total_tests"
    echo -e "Passed: ${GREEN}${TESTS_PASSED}${NC}"
    echo -e "Failed: ${RED}${TESTS_FAILED}${NC}"
    echo ""

    if [ "$TESTS_FAILED" -eq 0 ]; then
        echo -e "${GREEN}All tests PASSED!${NC}"
        echo "SMP boot appears to be working correctly."
        return 0
    else
        echo -e "${RED}Some tests FAILED.${NC}"
        echo "Check the serial output below for details:"
        echo ""
        echo "--- Serial Output ---"
        cat "$SERIAL_OUTPUT" 2>/dev/null || echo "(No output captured)"
        echo "--- End Output ---"
        return 1
    fi
}

# Main test execution
main() {
    print_header
    check_prerequisites
    run_qemu
    parse_boot_logs
    print_summary
}

main "$@"
