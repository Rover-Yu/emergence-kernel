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
import signal
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

# Global reference for signal handler cleanup
_output_filter = None


class TestResult:
    """Represents a single test result."""
    def __init__(self, name: str, passed: bool):
        self.name = name
        self.passed = passed


class TestFilter:
    """Filter QEMU serial output to show only test results in pretty format."""

    # Regex patterns for test results
    # klog outputs: [CPU<N>][<LEVEL>][<MODULE>] <message>
    TEST_PASSED_RE = re.compile(r'\[.*?\[TEST\] PASSED: (\S+)')
    TEST_FAILED_RE = re.compile(r'\[.*?\[TEST\] FAILED: (\S+)')
    SHUTDOWN_RE = re.compile(r'system is shutting down')

    def __init__(self, verbose: bool = False):
        self.verbose = verbose
        self.results: list[TestResult] = []
        self.terminal = TerminalOutput()
        self.shutdown_detected = False

    def filter(self, text: str, timed_out: bool = False) -> str:
        """Filter complete text output and return pretty formatted results.

        Args:
            text: Complete output from QEMU
            timed_out: True if test timed out (QEMU was killed)
        """
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

            # Check for normal shutdown
            if self.SHUTDOWN_RE.search(line):
                self.shutdown_detected = True

        # If timed out, add a timeout failure
        if timed_out:
            self.results.append(TestResult("_timeout_", False))

        return self._format_output(timed_out)

    def _format_output(self, timed_out: bool = False) -> str:
        """Format test results into pretty output.

        Args:
            timed_out: True if test timed out
        """
        if not self.results and not timed_out:
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
            if result.name == "_timeout_":
                status = ANSI.wrap("TIMEOUT", ANSI.RED)
                lines.append(f"  {ANSI.wrap('Test execution', ANSI.BOLD)}: {status}")
            elif result.passed:
                status = ANSI.wrap("PASS", ANSI.GREEN)
                lines.append(f"  {result.name}: {status}")
            else:
                status = ANSI.wrap("FAIL", ANSI.RED)
                lines.append(f"  {result.name}: {status}")

        # Summary
        passed = sum(1 for r in self.results if r.passed)
        failed = len(self.results) - passed

        lines.append("")
        lines.append(ANSI.wrap("-" * width, ANSI.DIM))
        lines.append("")

        if timed_out:
            summary = ANSI.wrap("  TIMEOUT: Test did not complete in time", ANSI.RED)
        elif failed == 0:
            summary = ANSI.wrap(f"  All {len(self.results)} tests passed", ANSI.GREEN)
        else:
            summary = f"  {ANSI.wrap(str(passed), ANSI.GREEN)} passed, {ANSI.wrap(str(failed), ANSI.RED)} failed"
        lines.append(summary)

        # Shutdown status
        if timed_out:
            lines.append("")
            lines.append(ANSI.wrap("  ⚠ QEMU was terminated due to timeout", ANSI.YELLOW))

        lines.append("")
        lines.append(ANSI.wrap("=" * width, ANSI.BOLD))
        lines.append("")

        return '\n'.join(lines) + '\n'

    def cleanup_tty(self) -> None:
        """Clean up TTY state after test execution.

        Resets terminal attributes and clears any leftover ANSI escape codes.
        This ensures the terminal is in a clean state after QEMU exits.
        """
        # Send terminal reset sequence
        sys.stdout.write("\033[0m")  # Reset all attributes
        # Clear screen (optional, commented out to preserve test output)
        # sys.stdout.write("\033[2J\033[H")
        sys.stdout.flush()


def main() -> int:
    """Run QEMU with timeout using the test framework.

    Returns:
        Exit code: 0 on success (normal shutdown), 1 on timeout, other on error
    """
    # Global reference to output_filter for signal handler
    global _output_filter
    _output_filter = None

    # Signal handler for cleanup on interrupt
    def cleanup_handler(signum, frame):
        """Clean up TTY before exit."""
        if _output_filter:
            _output_filter.cleanup_tty()
        sys.exit(130)  # Standard exit code for SIGINT (128 + 2)

    # Register cleanup handlers
    signal.signal(signal.SIGINT, cleanup_handler)
    signal.signal(signal.SIGTERM, cleanup_handler)

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

    # Store globally for signal handler cleanup
    _output_filter = output_filter

    # Debug mode: Run QEMU directly without timeout, with I/O passed through
    if args.debug:
        qemu_cmd = runner._build_qemu_command(args.cpus)
        if config.verbose:
            print(f"Running: {' '.join(qemu_cmd)}", file=sys.stderr)
        # Run QEMU in foreground with all I/O passed through
        result = subprocess.run(qemu_cmd)
        # Cleanup TTY before exit
        output_filter.cleanup_tty()
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

    # Determine if test timed out
    timed_out = (exit_code == 124)

    # Print output (filtered unless verbose)
    if args.verbose:
        sys.stdout.write(output_content)
        sys.stdout.flush()
    else:
        # Filter and print test results only (pass timeout status)
        filtered_output = output_filter.filter(output_content, timed_out=timed_out)
        sys.stdout.write(filtered_output)
        sys.stdout.flush()

    # Clean up TTY state before exit
    output_filter.cleanup_tty()

    # Return exit code: 0 for success, 1 for timeout (test failure)
    if timed_out:
        return 1  # Timeout is a test failure
    return 0  # Normal shutdown is success


if __name__ == "__main__":
    sys.exit(main())
