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


class TestFilter:
    """Filter QEMU serial output to show only test names and results."""

    # Markers for test output
    TEST_START_RE = re.compile(r'\[TEST\] Running test: (\S+)')
    TEST_PASSED_RE = re.compile(r'\[TEST\] PASSED: (\S+)')
    TEST_FAILED_RE = re.compile(r'\[TEST\] FAILED: (\S+)')
    TEST_HEADER_RE = re.compile(r'=+.*Test Suite=+')
    CSV_HEADER_RE = re.compile(r'CSV TEST LIST EXECUTION')
    UNIFIED_PASSED_RE = re.compile(r'UNIFIED: ALL TESTS PASSED')
    UNIFIED_FAILED_RE = re.compile(r'UNIFIED: SOME TESTS FAILED')
    SUMMARY_RE = re.compile(r'=+.*Summary=+')

    # Kernel boot markers (to show initial boot progress)
    KERNEL_INIT_RE = re.compile(
        r'\s*(' + '|'.join([
            r'KERNEL: [A-Z].+',
            r'SMP: [A-Z].+',
            r'PMM: [A-Z].+',
            r'MONITOR: [A-Z].+',
            r'CMDLINE: [A-Z].+',
            r'Multiboot: [A-Z].+',
        ]) + r')'
    )

    def __init__(self, verbose: bool = False):
        self.verbose = verbose
        self.in_test_suite = False
        self.test_name = None
        self.last_test = None
        self.buffer = []

    def _should_print_line(self, line: str) -> bool:
        """Check if line should be printed (not filtered)."""
        # Always print kernel init markers in verbose mode
        if self.verbose and self.KERNEL_INIT_RE.match(line):
            return True

        # Track when we're in a test suite
        if self.TEST_HEADER_RE.search(line) or self.CSV_HEADER_RE.search(line):
            self.in_test_suite = True
            return True  # Print test suite headers

        # Always print summary markers
        if self.SUMMARY_RE.search(line):
            return True

        # Track test execution
        if self.TEST_START_RE.search(line):
            match = self.TEST_START_RE.search(line)
            if match:
                self.test_name = match.group(1)
                self.in_test_suite = True
                return True  # Print test start markers

        # Always print test results
        if self.TEST_PASSED_RE.search(line) or self.TEST_FAILED_RE.search(line):
            return True

        # Always print unified summary
        if self.UNIFIED_PASSED_RE.search(line) or self.UNIFIED_FAILED_RE.search(line):
            return True

        # Filter everything else
        return False

    def filter_line(self, line: str) -> str:
        """Filter a line, returning empty string if filtered out."""
        if self._should_print_line(line):
            # Format test results with colored status
            match = self.TEST_PASSED_RE.search(line)
            if match:
                status = ANSI.wrap("PASS", ANSI.GREEN)
                test_name = match.group(1)
                return f"{test_name}: {status}"
            match = self.TEST_FAILED_RE.search(line)
            if match:
                status = ANSI.wrap("FAIL", ANSI.RED)
                test_name = match.group(1)
                return f"{test_name}: {status}"
            return line
        return ""

    def filter(self, text: str) -> str:
        """Filter complete text output."""
        lines = text.split('\n')
        filtered_lines = []

        for line in lines:
            filtered = self.filter_line(line)
            if filtered:  # Keep non-empty filtered lines
                filtered_lines.append(filtered)
            elif filtered.strip():  # Keep non-empty lines after filtering
                filtered_lines.append(line)

        # Join with newlines and ensure trailing newline
        result = '\n'.join(filtered_lines)
        if result and not result.endswith('\n'):
            result += '\n'
        return result

    def flush(self):
        """Flush any buffered output."""
        if self.buffer:
            output = ''.join(self.buffer)
            sys.stdout.write(output)
            sys.stdout.flush()
            self.buffer = []


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
        "--quiet",
        action="store_true",
        help="Suppress test start messages (still shows test results)"
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
        verbose=args.verbose,  # Pass verbose to config
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

    # Suppress real-time output when not verbose (quiet=True)
    # This allows us to filter the output before printing
    original_quiet = config.quiet
    if not args.verbose:
        config.quiet = True  # Suppress real-time callback

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
    # Exit code 124 indicates timeout (Python subprocess.TimeoutExpired)
    return 0 if exit_code != 124 else exit_code


if __name__ == "__main__":
    sys.exit(main())
