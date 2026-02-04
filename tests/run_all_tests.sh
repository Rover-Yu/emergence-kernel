#!/bin/bash
#
# run_all_tests.sh - Emergence Kernel Test Suite Runner
#
# This script runs all Emergence Kernel tests in sequence and provides
# a comprehensive summary of results.
#
# Usage: ./run_all_tests.sh
#

set -e

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Test list: "script_name:Test Display Name"
# Format: "filename:display_name[:cpu_arg]"
#
# NOTE: nk_protection_test.sh requires CONFIG_NK_PROTECTION_TESTS=1 to be enabled.
# Build with: make CONFIG_NK_PROTECTION_TESTS=1
# This test is NOT included in the default test suite.
TESTS=(
    "boot/boot_test.sh:Basic Kernel Boot"
    "pcd/pcd_test.sh:Page Control Data (PCD)"
    "nested_kernel_invariants/nested_kernel_invariants_test.sh:Nested Kernel Invariants"
    "readonly_visibility/readonly_visibility_test.sh:Read-Only Visibility"
    "timer/apic_timer_test.sh:APIC Timer"
    "smp/smp_boot_test.sh:SMP Boot:2"
)

# Global counters
SUITE_TOTAL=0
SUITE_PASSED=0
SUITE_FAILED=0
FAILED_TESTS=()

# Print suite header
print_suite_header() {
    echo ""
    echo "========================================"
    echo "Emergence Kernel Test Suite"
    echo "========================================"
    echo "Running ${#TESTS[@]} tests..."
    echo "========================================"
    echo ""
}

# Run a single test
run_single_test() {
    local script="$1"
    local name="$2"
    local cpu_arg="${3:-}"

    SUITE_TOTAL=$((SUITE_TOTAL + 1))
    local test_num="$SUITE_TOTAL"
    local total="${#TESTS[@]}"

    echo -e "${BLUE}[$test_num/$total]${NC} Running: $name..."

    # Change to tests directory
    cd "$SCRIPT_DIR"

    # Run the test with optional CPU argument
    if [ -n "$cpu_arg" ]; then
        if ./"$script" "$cpu_arg" > /tmp/emergence_test_output_$$ 2>&1; then
            echo -e "      ${GREEN}Test Result: PASS${NC}"
            SUITE_PASSED=$((SUITE_PASSED + 1))
        else
            local exit_code=$?
            echo -e "      ${RED}Test Result: FAIL${NC} (exit code: $exit_code)"
            SUITE_FAILED=$((SUITE_FAILED + 1))
            FAILED_TESTS+=("$name")
        fi
    else
        if ./"$script" > /tmp/emergence_test_output_$$ 2>&1; then
            echo -e "      ${GREEN}Test Result: PASS${NC}"
            SUITE_PASSED=$((SUITE_PASSED + 1))
        else
            local exit_code=$?
            echo -e "      ${RED}Test Result: FAIL${NC} (exit code: $exit_code)"
            SUITE_FAILED=$((SUITE_FAILED + 1))
            FAILED_TESTS+=("$name")
        fi
    fi

    # Clean up output file
    rm -f /tmp/emergence_test_output_$$

    echo ""
}

# Print suite summary
print_suite_summary() {
    echo "========================================"
    echo "Test Suite Summary"
    echo "========================================"
    echo "Total Tests: $SUITE_TOTAL"
    echo -e "Passed: ${GREEN}$SUITE_PASSED${NC}"
    echo -e "Failed: ${RED}$SUITE_FAILED${NC}"
    echo ""

    if [ "$SUITE_FAILED" -eq 0 ]; then
        echo -e "${GREEN}========================================${NC}"
        echo -e "${GREEN}ALL TESTS PASSED!${NC}"
        echo -e "${GREEN}========================================${NC}"
        return 0
    else
        echo -e "${RED}========================================${NC}"
        echo -e "${RED}SOME TESTS FAILED${NC}"
        echo -e "${RED}========================================${NC}"
        echo ""
        echo "Failed tests:"
        for test in "${FAILED_TESTS[@]}"; do
            echo -e "  ${RED}✗${NC} $test"
        done
        echo ""
        return 1
    fi
}

# Main execution
main() {
    print_suite_header

    # Run each test
    for test_spec in "${TESTS[@]}"; do
        IFS=':' read -r script name cpu_arg <<< "$test_spec"
        run_single_test "$script" "$name" "$cpu_arg"
    done

    # Print summary
    print_suite_summary
}

main "$@"
