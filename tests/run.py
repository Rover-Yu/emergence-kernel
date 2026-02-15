#!/usr/bin/env python3
"""
QEMU Runner Wrapper for 'make run' and 'make run-debug'

This script wraps the test framework's QEMURunner to provide timeout-based
execution for the 'make run' target and debug mode for 'make run-debug'.
It uses the same QEMU configuration as the test suite for consistency.

By default, filters QEMU serial output to show only test names and results.
Use --verbose to see full kernel serial output.

Note: For multiboot ISO, the kernel command line is embedded in the ISO
via grub.cfg, so we use run_qemu_direct() rather than run_qemu_with_env()
which would fail with -append/-cdrom incompatibility.
"""

import argparse
import re
import subprocess
import sys
import time
from pathlib import Path

# Add lib directory to path for imports
lib_dir = Path(__file__).parent / "lib"
sys.path.insert(0, str(lib_dir))

from config import TestConfig
from qemu_runner import QEMURunner
from output import TerminalOutput, ANSI


class TestResult:
    """Represents a single test result."""
    def __init__(self, name: str, passed: bool):
        self.name = name
        self.passed = passed


class TestFilter:
    """Filter QEMU serial output to show only test results in pretty format."""

    # Regex patterns for test results
    TEST_PASSED_RE = re.compile(r'\[TEST\] PASSED: (\S+)')
    TEST_FAILED_RE = re.compile(r'\[TEST\] FAILED: (\S+)')

    def __init__(self, verbose: bool = False):
        self.verbose = verbose
        self.results: list[TestResult] = []
        self.terminal = TerminalOutput()

    def filter(self, text: str) -> str:
        """Filter complete text output and return pretty formatted results."""
        lines = text.split('\n')

        for line in lines:
            # Check for PASSED
            match = self.TEST_PASSED_RE.search(line)
            if match:
                test_name = match.group(1)
                self.results.append(TestResult(test_name, True))

            # Check for FAILED
            match = self.TEST_FAILED_RE.search(line)
            if match:
                test_name = match.group(1)
                self.results.append(TestResult(test_name, False))

        return self._format_output()

    def _format_output(self) -> str:
        """Format test results into pretty output."""
        if not self.results:
            return ""

        lines = []
        width = 50

        # Header
        lines.append("")
        lines.append(ANSI.wrap("=" * width, ANSI.BOLD))
        lines.append(ANSI.wrap("  Test Results", ANSI.BOLD))
        lines.append(ANSI.wrap("=" * width, ANSI.BOLD))
        lines.append("")

        # Results
        for result in self.results:
            if result.passed:
                status = ANSI.wrap("PASS", ANSI.GREEN)
            else:
                status = ANSI.wrap("FAIL", ANSI.RED)
            lines.append(f"  {result.name}: {status}")

        # Summary
        passed = sum(1 for r in self.results if r.passed)
        failed = len(self.results) - passed

        lines.append("")
        lines.append(ANSI.wrap("-" * width, ANSI.DIM))
        lines.append("")

        if failed == 0:
            summary = ANSI.wrap(f"  All {len(self.results)} tests passed", ANSI.GREEN)
        else:
            summary = f"  {ANSI.wrap(str(passed), ANSI.GREEN)} passed, {ANSI.wrap(str(failed), ANSI.RED)} failed"
        lines.append(summary)

        lines.append("")
        lines.append(ANSI.wrap("=" * width, ANSI.BOLD))
        lines.append("")

        return '\n'.join(lines) + '\n'


def main() -> int:
    """Run QEMU with timeout using the test framework.

    Returns:
        Exit code from QEMU execution (0 on success, 124 on timeout, other on error)
    """
    parser = argparse.ArgumentParser(
        description="Run QEMU with timeout using the test framework"
    )
    parser.add_argument(
        "--timeout",
        type=int,
        default=8,
        help="Timeout in seconds (default: 8, ignored in debug mode)"
    )
    parser.add_argument(
        "--cpus",
        type=int,
        default=4,
        help="Number of CPUs (default: 4)"
    )
    parser.add_argument(
        "--debug",
        action="store_true",
        help="Enable debug mode (GDB server on :1234, freeze at start, no timeout)"
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Enable verbose output (show full kernel serial output)"
    )
    parser.add_argument(
        "--real-time",
        action="store_true",
        help="Enable real-time output streaming (for debugging)"
    )

    args = parser.parse_args()

    # Create test configuration
    # Note: Command line is embedded in ISO via KERNEL_CMDLINE (set by Makefile)
    config = TestConfig(
        qemu_timeout=args.timeout,
        cpu_count=args.cpus,
        debug_mode=args.debug,
        verbose=args.verbose,
        keep_output=False,
        real_time_output=args.real_time,
    )

    # Create QEMU runner and filter
    runner = QEMURunner(config)
    output_filter = TestFilter(verbose=args.verbose)

    # Debug mode: Run QEMU directly without timeout, with I/O passed through
    if args.debug:
        qemu_cmd = runner._build_qemu_command(args.cpus)
        if config.verbose:
            print(f"Running: {' '.join(qemu_cmd)}", file=sys.stderr)
        # Run QEMU in foreground with all I/O passed through
        result = subprocess.run(qemu_cmd)
        return result.returncode

    # Normal mode: Run with timeout and capture output
    start_time = time.time()

    # Suppress real-time output unless --real-time is explicitly requested
    original_quiet = config.quiet
    if not args.real_time:
        config.quiet = True

    # Use run_qemu_direct (command line is embedded in ISO via grub.cfg)
    output_content, exit_code = runner.run_qemu_direct(
        cpu_count=args.cpus,
        timeout=args.timeout
    )

    # Restore original quiet setting
    config.quiet = original_quiet

    duration = time.time() - start_time

    # Print output (filtered unless verbose)
    if args.verbose:
        sys.stdout.write(output_content)
        sys.stdout.flush()
    else:
        # Filter and print test results only
        filtered_output = output_filter.filter(output_content)
        sys.stdout.write(filtered_output)
        sys.stdout.flush()

    # Return 0 for success (matching original Makefile behavior: || exit 0)
    return 0 if exit_code != 124 else exit_code


if __name__ == "__main__":
    sys.exit(main())
