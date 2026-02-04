#!/bin/bash
#
# test_lib.sh - Reusable testing library for Emergence Kernel QEMU log tests
#
# This library provides common functions for running QEMU, capturing output,
# and making assertions about kernel behavior based on log analysis.
#

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Global counters
TESTS_PASSED=0
TESTS_FAILED=0

# Temporary output file (will be set by run_qemu_capture)
SERIAL_OUTPUT=""

# Kernel ISO path (must be set by calling script)
KERNEL_ISO="${KERNEL_ISO:-}"

# KVM enabled flag (set to 0 to disable KVM)
KVM_ENABLED="${KVM_ENABLED:-1}"

#
# check_prerequisites - Verify ISO and QEMU exist
#
# Parameters:
#   iso_path - Path to kernel ISO (optional, uses global KERNEL_ISO if not provided)
#
# Returns: 0 if all prerequisites are met, 1 otherwise
#
check_prerequisites() {
    local iso_path="${1:-$KERNEL_ISO}"
    local missing=0

    if [ -z "$iso_path" ]; then
        echo -e "${RED}Error: Kernel ISO path not set${NC}"
        echo "Set KERNEL_ISO variable before calling check_prerequisites"
        missing=1
    elif [ ! -f "$iso_path" ]; then
        echo -e "${RED}Error: Kernel ISO not found: $iso_path${NC}"
        echo "Please run 'make' first to build the kernel."
        missing=1
    fi

    if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
        echo -e "${RED}Error: qemu-system-x86_64 not found${NC}"
        echo "Please install QEMU."
        missing=1
    fi

    return $missing
}

#
# run_qemu_capture - Run QEMU and capture serial output
#
# Parameters:
#   cpu_count   - Number of CPUs (default: 1)
#   timeout     - Timeout in seconds (default: 5)
#   extra_flags - Additional QEMU flags (optional)
#
# Returns: Path to the output file (stored in SERIAL_OUTPUT global)
#
run_qemu_capture() {
    local cpu_count=${1:-1}
    local timeout=${2:-5}
    local extra_flags=${3:-""}
    local iso_path="${4:-$KERNEL_ISO}"

    # Create temp file for serial output
    # Use current directory instead of /tmp which may be read-only
    SERIAL_OUTPUT="./emergence_test_$$"

    # Build QEMU command
    local qemu_cmd="timeout ${timeout} qemu-system-x86_64"

    # Add KVM flag if enabled
    if [ "$KVM_ENABLED" = "1" ]; then
        qemu_cmd="$qemu_cmd -enable-kvm"
    fi

    # Run QEMU with serial output to file, timeout after specified seconds
    # Use -serial stdio and redirect to file instead of -serial file:
    # Include shutdown device for clean VM exit (8-bit I/O for exit code 0)
    $qemu_cmd \
        -M pc \
        -m 128M \
        -nographic \
        -monitor none \
        -smp ${cpu_count} \
        -cdrom "${iso_path}" \
        ${extra_flags} \
        -device isa-debug-exit,iobase=0xB004,iosize=1 \
        -serial stdio >"${SERIAL_OUTPUT}" 2>&1 || true

    # Check if output was captured
    if [ ! -f "$SERIAL_OUTPUT" ]; then
        echo -e "${RED}Error: Failed to capture serial output${NC}" >&2
        return 1
    fi

    # Output just the file path to stdout
    echo "$SERIAL_OUTPUT"
}

#
# assert_pattern_exists - Check if pattern exists in output
#
# Parameters:
#   pattern     - Grep pattern to search for
#   description - Human-readable description
#   file        - File to search (default: $SERIAL_OUTPUT)
#
# Returns: 0 if pattern found, 1 otherwise
#
assert_pattern_exists() {
    local pattern="$1"
    local description="$2"
    local file="${3:-$SERIAL_OUTPUT}"

    if grep -q "$pattern" "$file" 2>/dev/null; then
        print_result "$description" "true"
        return 0
    else
        print_result "$description" "false" "Pattern not found: $pattern"
        return 1
    fi
}

#
# assert_pattern_count - Check pattern count matches expected
#
# Parameters:
#   pattern     - Grep pattern to search for
#   expected    - Expected count
#   description - Human-readable description
#   file        - File to search (default: $SERIAL_OUTPUT)
#
# Returns: 0 if count matches, 1 otherwise
#
assert_pattern_count() {
    local pattern="$1"
    local expected="$2"
    local description="$3"
    local file="${4:-$SERIAL_OUTPUT}"

    local actual=$(grep -c "$pattern" "$file" 2>/dev/null || echo "0")

    if [ "$actual" -eq "$expected" ]; then
        print_result "$description" "true" "$actual matches expected"
        return 0
    else
        print_result "$description" "false" "Expected $expected, found $actual"
        return 1
    fi
}

#
# assert_pattern_at_least - Check pattern count is at least minimum
#
# Parameters:
#   pattern     - Grep pattern to search for
#   minimum     - Minimum expected count
#   description - Human-readable description
#   file        - File to search (default: $SERIAL_OUTPUT)
#
# Returns: 0 if count >= minimum, 1 otherwise
#
assert_pattern_at_least() {
    local pattern="$1"
    local minimum="$2"
    local description="$3"
    local file="${4:-$SERIAL_OUTPUT}"

    local actual=$(grep -c "$pattern" "$file" 2>/dev/null || echo "0")

    if [ "$actual" -ge "$minimum" ]; then
        print_result "$description" "true" "$actual >= $minimum"
        return 0
    else
        print_result "$description" "false" "Expected >= $minimum, found $actual"
        return 1
    fi
}

#
# assert_timer_quotes - Check timer quotes count
#
# Parameters:
#   timer_type  - "APIC" or "RTC"
#   expected    - Expected quote count (usually 5)
#   file        - File to search (default: $SERIAL_OUTPUT)
#
# Returns: 0 if count matches, 1 otherwise
#
assert_timer_quotes() {
    local timer_type="$1"
    local expected="$2"
    local file="${3:-$SERIAL_OUTPUT}"

    local actual=$(grep -c "\[${timer_type}\]" "$file" 2>/dev/null || echo "0")

    if [ "$actual" -eq "$expected" ]; then
        print_result "${timer_type} timer quotes" "true" "$actual quotes"
        return 0
    else
        print_result "${timer_type} timer quotes" "false" "Expected $expected quotes, found $actual"
        return 1
    fi
}

#
# count_char_occurrences - Count character occurrences in file
#
# Parameters:
#   char  - Single character to count
#   file  - File to search (default: $SERIAL_OUTPUT)
#
# Returns: Character count
#
count_char_occurrences() {
    local char="$1"
    local file="${2:-$SERIAL_OUTPUT}"

    # Remove newlines, count the char
    local content=$(tr -d '\n' < "$file" 2>/dev/null || echo "")
    local count=0

    for (( i=0; i<${#content}; i++ )); do
        if [ "${content:$i:1}" = "$char" ]; then
            count=$((count + 1))
        fi
    done

    echo $count
}

#
# assert_debug_char_count - Check debug character count (A, R, X)
#
# Parameters:
#   char        - Debug character ('A', 'R', or 'X')
#   minimum     - Minimum expected count
#   description - Human-readable description
#   file        - File to search (default: $SERIAL_OUTPUT)
#
# Returns: 0 if count >= minimum, 1 otherwise
#
assert_debug_char_count() {
    local char="$1"
    local minimum="$2"
    local description="$3"
    local file="${4:-$SERIAL_OUTPUT}"

    local actual=$(count_char_occurrences "$char" "$file")

    if [ "$actual" -ge "$minimum" ]; then
        print_result "$description" "true" "$actual '$char' chars >= $minimum"
        return 0
    else
        print_result "$description" "false" "Expected >= $minimum '$char' chars, found $actual"
        return 1
    fi
}

#
# assert_no_exceptions - Check that no exception characters present
#
# Parameters:
#   file  - File to search (default: $SERIAL_OUTPUT)
#
# Returns: 0 if no exceptions, 1 if exceptions found
#
assert_no_exceptions() {
    local file="${1:-$SERIAL_OUTPUT}"

    # Count only standalone 'X' debug characters (lines containing only A, R, X)
    # Exception ISRs output 'X' as a debug character. We exclude X from string literals.
    local exception_count=$(grep -E '^[ARX]+$' "$file" 2>/dev/null | tr -cd 'X' | wc -c)

    if [ "$exception_count" -eq 0 ]; then
        print_result "No exceptions" "true"
        return 0
    else
        print_result "No exceptions" "false" "Found $exception_count exception indicators ('X')"
        return 1
    fi
}

#
# print_result - Print colored test result
#
# Parameters:
#   name    - Test name
#   passed  - "true" or "false"
#   details - Optional details string
#
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

#
# print_summary - Print test summary with exit code
#
# Returns: 0 if all tests passed, 1 otherwise
#
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
        return 0
    else
        echo -e "${RED}Some tests FAILED.${NC}"
        if [ -n "$SERIAL_OUTPUT" ] && [ -f "$SERIAL_OUTPUT" ]; then
            echo ""
            echo "--- Serial Output ---"
            cat "$SERIAL_OUTPUT" 2>/dev/null || echo "(No output captured)"
            echo "--- End Output ---"
        fi
        return 1
    fi
}

#
# cleanup - Remove temporary files
#
cleanup() {
    if [ -n "$SERIAL_OUTPUT" ] && [ -f "$SERIAL_OUTPUT" ]; then
        rm -f "$SERIAL_OUTPUT"
    fi
}

#
# setup_trap - Set up cleanup trap
#
setup_trap() {
    trap cleanup EXIT
}
