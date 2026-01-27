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
KERNEL_ISO="${SCRIPT_DIR}/../jakernel.iso"
QEMU_TIMEOUT=8

# Source test library (KERNEL_ISO must be set before sourcing)
source "${SCRIPT_DIR}/test_lib.sh"

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

    # Test 1: APIC timer initialization messages present
    if echo "$boot_log" | grep -q "APIC:"; then
        print_result "APIC timer initialization" "true" "Timer init messages found"
    else
        print_result "APIC timer initialization" "false" "No timer init messages found"
    fi

    # Test 2: APIC timer quote count (should be 4 quotes)
    local apic_quotes=$(echo "$boot_log" | grep -c "APIC:" || echo "0")
    if [ "$apic_quotes" -ge 4 ]; then
        print_result "APIC timer quotes" "true" "$apic_quotes quotes (>= 4)"
    else
        print_result "APIC timer quotes" "false" "Expected >= 4 quotes, found $apic_quotes"
    fi

    # Test 3: Debug character 'A' count (APIC timer ISR debug char)
    assert_debug_char_count "A" 5 "APIC timer ISR debug characters" "$output_file"

    # Test 4: Verify mathematician quotes content
    if echo "$boot_log" | grep -q "Mathematics is queen of sciences"; then
        print_result "Mathematician quote content" "true" "Quote content found"
    else
        print_result "Mathematician quote content" "false" "Quote content not found"
    fi

    # Test 5: No exceptions
    assert_no_exceptions "$output_file"

    print_summary
}

main "$@"
