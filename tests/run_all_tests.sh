#!/bin/bash
#
# run_all_tests.sh - Emergence Kernel Test Suite Runner
#
# This script runs all Emergence Kernel tests in sequence and provides
# a comprehensive summary of results.
#
# Usage: ./run_all_tests.sh [OPTIONS]
#
# Options:
#   --verbose, -v    Show test output in real-time
#   --keep-output    Keep all test output files (saved to test_results/)
#   --help, -h       Show this help message
#

set -e

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Options
VERBOSE=false
KEEP_ALL_OUTPUT=false

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --verbose|-v)
            VERBOSE=true
            shift
            ;;
        --keep-output)
            KEEP_ALL_OUTPUT=true
            shift
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --verbose, -v    Show test output in real-time"
            echo "  --keep-output    Keep all test output files (saved to test_results/)"
            echo "  --help, -h       Show this help message"
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Test list: "script_name:Test Display Name"
# Format: "filename:display_name[:cpu_arg]"
#
# NOTE: nk_protection_test.sh requires CONFIG_NK_PROTECTION_TESTS=1 to be enabled.
# Build with: make CONFIG_NK_PROTECTION_TESTS=1
# This test is NOT included in the default test suite.
TESTS=(
    "boot/boot_test.sh:Basic Kernel Boot"
    "pcd/pcd_test.sh:Page Control Data (PCD)"
    "slab/slab_test.sh:Slab Allocator"
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
declare -A FAILED_TEST_OUTPUTS  # Store output file path for each failed test

# Create results directory
RESULTS_DIR="$SCRIPT_DIR/test_results"
mkdir -p "$RESULTS_DIR"

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

    # Create a safe test name for filenames
    local safe_name=$(echo "$name" | tr '[:upper:]' '[:lower:]' | tr ' ' '_')
    local output_file="$RESULTS_DIR/${safe_name}_output_$$.txt"

    echo -e "${BLUE}[$test_num/$total]${NC} Running: $name..."

    # Check if script exists
    if [ ! -f "$SCRIPT_DIR/$script" ]; then
        echo -e "      ${RED}Test Result: FAIL${NC}"
        echo -e "      ${RED}Error: Test script not found: $SCRIPT_DIR/$script${NC}"
        SUITE_FAILED=$((SUITE_FAILED + 1))
        FAILED_TESTS+=("$name")
        FAILED_TEST_OUTPUTS["$name"]="Script not found: $SCRIPT_DIR/$script"
        echo ""
        return 1
    fi

    # Check if ISO exists
    local iso_path="$PROJECT_ROOT/emergence.iso"
    if [ ! -f "$iso_path" ]; then
        echo -e "      ${RED}Test Result: FAIL${NC}"
        echo -e "      ${RED}Error: Kernel ISO not found: $iso_path${NC}"
        echo -e "      ${YELLOW}Hint: Run 'make' first to build the kernel${NC}"
        SUITE_FAILED=$((SUITE_FAILED + 1))
        FAILED_TESTS+=("$name")
        FAILED_TEST_OUTPUTS["$name"]="ISO not found at: $iso_path"
        echo ""
        return 1
    fi

    # Change to tests directory
    cd "$SCRIPT_DIR"

    # Run the test with optional CPU argument
    local test_passed=false
    local exit_code=0

    if [ "$VERBOSE" = true ]; then
        # Verbose mode: show output in real-time
        echo -e "${CYAN}--- Test output start ---${NC}"
        if [ -n "$cpu_arg" ]; then
            if ./"$script" "$cpu_arg" 2>&1 | tee "$output_file"; then
                test_passed=true
            else
                exit_code=$?
            fi
        else
            if ./"$script" 2>&1 | tee "$output_file"; then
                test_passed=true
            else
                exit_code=$?
            fi
        fi
        echo -e "${CYAN}--- Test output end ---${NC}"
    else
        # Normal mode: capture output silently
        if [ -n "$cpu_arg" ]; then
            if ./"$script" "$cpu_arg" > "$output_file" 2>&1; then
                test_passed=true
            else
                exit_code=$?
            fi
        else
            if ./"$script" > "$output_file" 2>&1; then
                test_passed=true
            else
                exit_code=$?
            fi
        fi
    fi

    if [ "$test_passed" = true ]; then
        echo -e "      ${GREEN}Test Result: PASS${NC}"
        SUITE_PASSED=$((SUITE_PASSED + 1))
        # Clean up passed test output unless --keep-output was specified
        if [ "$KEEP_ALL_OUTPUT" = false ]; then
            rm -f "$output_file"
        fi
    else
        echo -e "      ${RED}Test Result: FAIL${NC} (exit code: $exit_code)"
        SUITE_FAILED=$((SUITE_FAILED + 1))
        FAILED_TESTS+=("$name")
        FAILED_TEST_OUTPUTS["$name"]="$output_file"

        # Show a snippet of the failure
        if [ -f "$output_file" ]; then
            echo -e "      ${YELLOW}Last 30 lines of output:${NC}"
            tail -30 "$output_file" | sed 's/^/      /'
            echo -e "      ${CYAN}Full output saved to: $output_file${NC}"
        fi
    fi

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
        if [ "$KEEP_ALL_OUTPUT" = true ]; then
            echo ""
            echo -e "${CYAN}Test outputs saved to: $RESULTS_DIR${NC}"
        fi
        return 0
    else
        echo -e "${RED}========================================${NC}"
        echo -e "${RED}SOME TESTS FAILED${NC}"
        echo -e "${RED}========================================${NC}"
        echo ""
        echo "Failed tests:"
        for test in "${FAILED_TESTS[@]}"; do
            local output_info="${FAILED_TEST_OUTPUTS[$test]}"
            echo -e "  ${RED}✗${NC} $test"
            if [ -f "$output_info" ]; then
                echo -e "    ${CYAN}Output: $output_info${NC}"
            else
                echo -e "    ${YELLOW}Error: $output_info${NC}"
            fi
        done
        echo ""
        echo -e "${YELLOW}To view full output of a failed test:${NC}"
        echo -e "  ${CYAN}cat $RESULTS_DIR/<test_name>_output_*.txt${NC}"
        echo ""
        echo -e "${YELLOW}To re-run a single test with verbose output:${NC}"
        echo -e "  ${CYAN}cd tests && ./<test_script>${NC}"
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
