#!/usr/bin/env python3
"""
Nested Kernel Invariants Test

Tests invariants related to the nested kernel mapping protection.
This verifies that kernel memory protections are working correctly.
"""

import sys
import argparse
from pathlib import Path

# Add lib directory to path for imports
lib_path = Path(__file__).parent.parent / "lib"
sys.path.insert(0, str(lib_path))

from test_framework import TestFramework, TestConfig, create_framework
from output import TerminalOutput, ANSI


def parse_arguments():
    """Parse command line arguments."""
    parser = argparse.ArgumentParser(
        description="Nested Kernel Invariants Test",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
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
        default=3,
        metavar="SECONDS",
        help="QEMU timeout in seconds (default: 3)"
    )
    return parser.parse_args()


def main():
    """Main test execution."""
    args = parse_arguments()

    output = TerminalOutput()

    # Print test header
    output.print_header("Nested Kernel Invariants Test", width=40)
    print(f"CPU Count: 1")
    print(f"Timeout: {args.timeout} seconds")
    print()

    # Create framework and run test
    framework = create_framework(
        test_name="nested_kernel_invariants",
        cpu_count=1,
        timeout=args.timeout,
        verbose=args.verbose,
        keep_output=args.keep_output
    )

    # Check prerequisites
    if not framework.check_prerequisites():
        output.print_error("Prerequisites not met")
        sys.exit(1)

    # Run the test
    print("Starting QEMU with 1 CPU...")
    print()

    if framework.run_test("nested_kernel_invariants", cpu_count=1):
        exit_code = framework.print_summary()
    else:
        exit_code = 1

    sys.exit(exit_code)


if __name__ == "__main__":
    main()
