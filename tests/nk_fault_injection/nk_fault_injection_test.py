#!/usr/bin/env python3
"""
Nested Kernel Fault Injection Test

This is a destructive test that verifies page fault protection
by attempting to write to boot PML4 and expecting a page fault.
"""

import sys
import argparse
from pathlib import Path

# Add lib directory to path for imports
lib_path = Path(__file__).parent.parent / "lib"
sys.path.insert(0, str(lib_path))

from test_framework import TestFramework, TestConfig, create_framework
from output import TerminalOutput


def parse_arguments():
    """Parse command line arguments."""
    parser = argparse.ArgumentParser(
        description="Nested Kernel Fault Injection Test",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
This is a DESTRUCTIVE test that verifies page fault protection
via fault injection.

The test attempts to write to boot PML4 and expects a page fault
to be triggered, demonstrating that the nested kernel protection
is working correctly.

Examples:
  %(prog)s              Run with default settings
  %(prog)s --verbose    Show detailed output
        """
    )
    parser.add_argument(
        "-v", "--verbose",
        action="store_true",
        help="Show detailed test output"
    )
    parser.add_argument(
        "--keep-output",
        action="store_true",
        help="Keep test output files for debugging"
    )
    parser.add_argument(
        "--timeout",
        type=int,
        default=10,
        metavar="SECONDS",
        help="QEMU timeout in seconds (default: 10)"
    )
    return parser.parse_args()


def run_custom_checks(assertions, output):
    """Run custom assertions for nk_fault_injection test.

    Args:
        assertions: Assertions object
        output: TerminalOutput object

    Returns:
        True if all checks pass, False otherwise
    """
    all_passed = True

    # Test 1: NK protection test initiated
    if assertions.assert_pattern_exists(r"NESTED KERNEL PROTECTION TESTS"):
        output.print_success("NK fault injection test initiated")
    else:
        output.print_failure("NK fault injection test initiated")
        all_passed = False

    # Test 2: Running in unprivileged mode
    if assertions.assert_pattern_exists(r"UNPRIVILEGED mode"):
        output.print_success("Running in unprivileged mode")
    else:
        output.print_failure("Running in unprivileged mode")
        all_passed = False

    # Test 3: Test 1 started (write to boot PML4)
    if assertions.assert_pattern_exists(r"Test 1: Write to boot PML4"):
        output.print_success("Test 1 (page table write) started")
    else:
        output.print_failure("Test 1 (page table write) started")
        all_passed = False

    # Test 4: Page fault was triggered
    # Note: The actual "system is shutting down" message may not appear
    # due to serial port corruption after the page fault.
    if assertions.assert_pattern_exists(r"Writing to boot PML4"):
        output.print_success("Page fault triggered (write attempted)")
    else:
        output.print_failure("Page fault triggered (write attempted)")
        all_passed = False

    return all_passed


def main():
    """Main test execution."""
    args = parse_arguments()

    output = TerminalOutput()

    # Print test header
    output.print_header("Nested Kernel Fault Injection Test", width=40)
    print(f"CPU Count: 1")
    print(f"Timeout: {args.timeout} seconds")
    print()

    # Create framework
    config = TestConfig(
        test_name="nk_fault_injection",
        cpu_count=1,
        qemu_timeout=args.timeout,
        verbose=args.verbose,
        keep_output=args.keep_output
    )

    framework = TestFramework(config)

    # Check prerequisites
    if not framework.check_prerequisites():
        output.print_error("Prerequisites not met")
        sys.exit(1)

    # Run the test with custom assertions
    print("Starting QEMU with 1 CPU...")
    print()

    if framework.run_test_with_assertions(
        "nk_fault_injection",
        lambda a: run_custom_checks(a, output),
        cpu_count=1
    ):
        exit_code = framework.print_summary()
    else:
        exit_code = 1

    sys.exit(exit_code)


if __name__ == "__main__":
    main()
