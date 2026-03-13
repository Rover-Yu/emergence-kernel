#!/usr/bin/env python3
"""
KMAP Subsystem Test

Tests the kernel virtual memory mapping subsystem which tracks memory
regions, manages page tables, and provides demand paging.
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
        description="KMAP Subsystem Test",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s              Run with 2 CPUs (default)
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
        default=8,
        metavar="SECONDS",
        help="QEMU timeout in seconds (default: 8)"
    )
    parser.add_argument(
        "--cpus",
        type=int,
        default=2,
        metavar="COUNT",
        help="Number of CPUs to use (default: 2)"
    )
    parser.add_argument(
        "-q", "--quiet",
        action="store_true",
        help="Suppress header/footer, show only result"
    )
    return parser.parse_args()


def main():
    """Main test execution."""
    args = parse_arguments()

    output = TerminalOutput()

    # Print test header (skip in quiet mode)
    if not args.quiet:
        output.print_header("KMAP Subsystem Test", width=40)
        print(f"CPU Count: {args.cpus}")
        print(f"Timeout: {args.timeout} seconds")
        print()

    # Create framework and run test
    framework = create_framework(
        test_name="kmap",
        cpu_count=args.cpus,
        timeout=args.timeout,
        verbose=args.verbose,
        keep_output=args.keep_output,
        quiet=args.quiet
    )

    # Check prerequisites
    if not framework.check_prerequisites():
        output.print_error("Prerequisites not met")
        sys.exit(1)

    # Run the test
    if not args.quiet:
        print(f"Starting QEMU with {args.cpus} CPU(s)...")
        print()

    if framework.run_test("kmap", cpu_count=args.cpus):
        exit_code = framework.print_summary()
    else:
        exit_code = 1

    sys.exit(exit_code)


if __name__ == "__main__":
    main()
