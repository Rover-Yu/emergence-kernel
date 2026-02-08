#!/usr/bin/env python3
"""
Emergence Kernel Test Suite Runner

Runs all tests in sequence and provides comprehensive summary.
"""

import sys
import argparse
import time
from pathlib import Path

# Add lib directory to path
sys.path.insert(0, str(Path(__file__).parent / "lib"))

from test_framework import TestFramework, TestConfig
from config import TestSuiteSummary
from output import TerminalOutput, ANSI, print_progress

# Test configuration
# Format: (script_path, test_name, cpu_count, timeout)
TESTS = [
    ("boot/boot_test.py", "Basic Kernel Boot", "boot", 1, 3),
    ("pcd/pcd_test.py", "Page Control Data (PCD)", "pcd", 1, 3),
    ("slab/slab_test.py", "Slab Allocator", "slab", 2, 5),
    ("nested_kernel_invariants/nested_kernel_invariants_test.py",
     "Nested Kernel Invariants", "nested_kernel_invariants", 1, 3),
    ("readonly_visibility/readonly_visibility_test.py",
     "Read-Only Visibility", "readonly_visibility", 1, 3),
    ("timer/apic_timer_test.py", "APIC Timer", "timer", 1, 5),
    ("smp/smp_boot_test.py", "SMP Boot", "smp", 2, 5),
    # NOTE: nk_protection test is disabled by default
    # It requires CONFIG_NK_PROTECTION_TESTS=1 to be compiled into kernel
    # Run manually with: python3 tests/nk_protection/nk_protection_test.py
    # ("nk_protection/nk_protection_test.py",
    #  "Nested Kernel Mappings Protection", "nk_protection", 1, 10),
]


def parse_arguments():
    """Parse command line arguments."""
    parser = argparse.ArgumentParser(
        description="Emergence Kernel Test Suite Runner",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s                  Run all tests
  %(prog)s --verbose        Show detailed output
  %(prog)s --keep-output    Keep all test output files
  %(prog)s --list           List available tests
        """
    )
    parser.add_argument(
        "-v", "--verbose",
        action="store_true",
        help="Show test output in real-time"
    )
    parser.add_argument(
        "--keep-output",
        action="store_true",
        help="Keep all test output files"
    )
    parser.add_argument(
        "--list",
        action="store_true",
        help="List available tests and exit"
    )
    parser.add_argument(
        "--test",
        type=str,
        metavar="NAME",
        help="Run only the specified test (by test name)"
    )
    return parser.parse_args()


def list_tests():
    """List available tests."""
    output = TerminalOutput()
    output.print_header("Available Tests", width=40)

    for i, (script_path, description, test_name, cpu_count, timeout) in enumerate(TESTS, 1):
        print(f"{i}. {description}")
        print(f"   Test Name: {test_name}")
        print(f"   CPUs: {cpu_count}, Timeout: {timeout}s")
        print(f"   Script: {script_path}")
        print()


def run_test_script(script_path: Path, verbose: bool, keep_output: bool) -> bool:
    """Run a test script directly.

    Args:
        script_path: Path to the test script
        verbose: Whether to enable verbose output
        keep_output: Whether to keep output files

    Returns:
        True if test passed, False otherwise
    """
    import subprocess

    cmd = [sys.executable, str(script_path)]

    if verbose:
        cmd.append("--verbose")

    if keep_output:
        cmd.append("--keep-output")

    result = subprocess.run(cmd, cwd=Path(__file__).parent)

    return result.returncode == 0


def main():
    """Main execution."""
    args = parse_arguments()

    output = TerminalOutput()

    if args.list:
        list_tests()
        sys.exit(0)

    # Filter tests if --test specified
    tests_to_run = TESTS
    if args.test:
        tests_to_run = [t for t in TESTS if t[2] == args.test]
        if not tests_to_run:
            output.print_error(f"Test '{args.test}' not found")
            print(f"Available tests: {', '.join(t[2] for t in TESTS)}")
            sys.exit(1)

    # Print suite header
    print()
    print("=" * 40)
    print("Emergence Kernel Test Suite")
    print("=" * 40)
    print(f"Running {len(tests_to_run)} test(s)...")
    print("=" * 40)
    print()

    # Create test suite summary
    summary = TestSuiteSummary()
    tests_dir = Path(__file__).parent

    # Track results
    results = []

    # Run each test
    for i, (script_path, description, test_name, cpu_count, timeout) in enumerate(tests_to_run, 1):
        test_script = tests_dir / script_path

        # Check if script exists
        if not test_script.exists():
            output.print_failure(f"Test script not found: {script_path}")
            results.append((test_name, False, "Script not found"))
            summary.failed += 1
            summary.total_tests += 1
            continue

        # Print progress
        print_progress(i, len(tests_to_run), description)

        # Run the test
        import subprocess

        cmd = [sys.executable, str(test_script)]

        if args.verbose:
            cmd.append("--verbose")

        if args.keep_output:
            cmd.append("--keep-output")

        start_time = time.time()
        result = subprocess.run(cmd, cwd=tests_dir)
        duration = time.time() - start_time

        passed = result.returncode == 0
        results.append((test_name, passed, duration))

        if passed:
            summary.passed += 1
        else:
            summary.failed += 1
        summary.total_tests += 1

        print()

    # Print summary
    print()
    output.print_summary(summary.total_tests, summary.passed, summary.failed)

    # Print individual results
    if results:
        print("\nIndividual Test Results:")
        print("-" * 40)

        for test_name, passed, duration_or_msg in results:
            if isinstance(duration_or_msg, str):
                # Error message
                status = "FAIL"
                duration_str = "N/A"
            else:
                duration_str = f"{duration_or_msg:.2f}s"
                status = "PASS" if passed else "FAIL"

            status_colored = ANSI.wrap(status, ANSI.GREEN if passed else ANSI.RED)
            print(f"{test_name:30s} {status_colored:8s} {duration_str}")

    # Print total duration
    total_duration = sum(
        d for _, _, d in results
        if isinstance(d, (int, float))
    )
    if total_duration > 0:
        print(f"\nTotal Duration: {total_duration:.2f}s")

    # Exit with appropriate code
    sys.exit(0 if summary.failed == 0 else 1)


if __name__ == "__main__":
    main()
