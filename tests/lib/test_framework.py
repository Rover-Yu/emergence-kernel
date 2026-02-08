"""
Main Test Framework

Provides the primary TestFramework class that integrates all modules.
"""

import signal
import sys
import time
from pathlib import Path
from typing import List, Optional

try:
    from .config import TestConfig, TestResult
    from .output import TerminalOutput, ANSI, print_progress
    from .qemu_runner import QEMURunner, find_qemu_binary, check_qemu_version
    from .assertions import Assertions
except ImportError:
    from config import TestConfig, TestResult
    from output import TerminalOutput, ANSI, print_progress
    from qemu_runner import QEMURunner, find_qemu_binary, check_qemu_version
    from assertions import Assertions


class TestFramework:
    """Main test framework class.

    This class orchestrates test execution, providing a high-level API
    for running kernel tests with proper resource management and reporting.
    """

    def __init__(self, config: Optional[TestConfig] = None):
        """Initialize test framework.

        Args:
            config: Optional test configuration (uses defaults if None)
        """
        self.config = config or TestConfig()
        self.runner = QEMURunner(self.config)
        self.output = TerminalOutput()

        # Test counters
        self.tests_passed = 0
        self.tests_failed = 0
        self.test_results: List[TestResult] = []

        # Track if we've checked prerequisites
        self._prerequisites_checked = False

    def check_prerequisites(self) -> bool:
        """Verify ISO exists and QEMU is available.

        This method checks that the kernel ISO exists and QEMU is
        installed and accessible.

        Returns:
            True if all prerequisites met, False otherwise
        """
        if self._prerequisites_checked:
            return True

        # Check ISO exists
        if not self.config.kernel_iso.exists():
            self.output.print_failure(
                "Kernel ISO not found",
                str(self.config.kernel_iso)
            )
            self.output.print_info("Hint: Run 'make' first to build the kernel")
            return False

        # Check QEMU is available
        qemu_binary = find_qemu_binary()
        if not qemu_binary:
            self.output.print_failure("qemu-system-x86_64 not found")
            self.output.print_info("Please install QEMU")
            return False

        # Check QEMU version if possible
        if not check_qemu_version():
            self.output.print_warning(
                "Could not verify QEMU version. "
                "Tests may fail with older QEMU versions."
            )

        self._prerequisites_checked = True
        return True

    def run_test(self, test_name: str, cpu_count: Optional[int] = None) -> bool:
        """Run a single test by name.

        This method executes a test with the specified parameters,
        captures the output, and verifies the result.

        Args:
            test_name: Name of test (e.g., 'boot', 'smp', 'slab')
            cpu_count: Number of CPUs to use (defaults to config value)

        Returns:
            True if test passed, False otherwise
        """
        if cpu_count is None:
            cpu_count = self.config.cpu_count

        if not self.check_prerequisites():
            return False

        cmdline = self.config.get_qemu_cmdline(test_name)

        # Print test start (skip in quiet mode)
        if not self.config.quiet:
            self.output.print_test_start(
                test_name,
                cpu_count,
                self.config.qemu_timeout
            )

        # Run test with timing
        start_time = time.time()

        output_content, exit_code = self.runner.run_via_make(
            cmdline,
            self.config.qemu_timeout
        )

        duration = time.time() - start_time

        # Verify result
        assertions = Assertions(output_content)

        if assertions.assert_test_passed(test_name):
            if not self.config.quiet:
                self.output.print_success(f"{test_name} test passed")
            self.tests_passed += 1
            self.test_results.append(TestResult(
                test_name=test_name,
                passed=True,
                exit_code=exit_code,
                duration=duration,
                message=""
            ))
            return True
        else:
            # Determine failure reason
            failure_reason = "PASSED marker not found in output"

            if assertions.assert_test_failed(test_name):
                failure_reason = "Test explicitly reported failure"

            if not self.config.quiet:
                self.output.print_failure(
                    f"{test_name} test failed",
                    failure_reason
                )

            if not self.config.quiet and self.config.verbose:
                self.output.print_warning("\nFailure output:")
                self.output.print_verbose(assertions.get_failure_snippet())

            self.tests_failed += 1
            self.test_results.append(TestResult(
                test_name=test_name,
                passed=False,
                exit_code=exit_code,
                duration=duration,
                message=failure_reason
            ))
            return False

    def run_test_with_assertions(
        self,
        test_name: str,
        custom_assertions: callable,
        cpu_count: Optional[int] = None
    ) -> bool:
        """Run a test with custom assertion logic.

        This method allows for custom verification logic beyond the
        standard PASSED marker check.

        Args:
            test_name: Name of the test
            custom_assertions: Function that takes Assertions and returns bool
            cpu_count: Number of CPUs to use

        Returns:
            True if test passed, False otherwise
        """
        if cpu_count is None:
            cpu_count = self.config.cpu_count

        if not self.check_prerequisites():
            return False

        cmdline = self.config.get_qemu_cmdline(test_name)

        if not self.config.quiet:
            self.output.print_test_start(
                test_name,
                cpu_count,
                self.config.qemu_timeout
            )

        start_time = time.time()

        output_content, exit_code = self.runner.run_via_make(
            cmdline,
            self.config.qemu_timeout
        )

        duration = time.time() - start_time

        # Run custom assertions
        assertions = Assertions(output_content)

        try:
            if custom_assertions(assertions):
                if not self.config.quiet:
                    self.output.print_success(f"{test_name} test passed")
                self.tests_passed += 1
                self.test_results.append(TestResult(
                    test_name=test_name,
                    passed=True,
                    exit_code=exit_code,
                    duration=duration,
                    message=""
                ))
                return True
            else:
                if not self.config.quiet:
                    self.output.print_failure(
                        f"{test_name} test failed",
                        "Custom assertions failed"
                    )

                if not self.config.quiet and self.config.verbose:
                    self.output.print_warning("\nFailure output:")
                    self.output.print_verbose(assertions.get_failure_snippet())

                self.tests_failed += 1
                self.test_results.append(TestResult(
                    test_name=test_name,
                    passed=False,
                    exit_code=exit_code,
                    duration=duration,
                    message="Custom assertions failed"
                ))
                return False

        except Exception as e:
            self.output.print_failure(
                f"{test_name} test failed",
                f"Exception during assertions: {e}"
            )
            self.tests_failed += 1
            return False

    def print_summary(self) -> int:
        """Print test summary.

        Returns:
            Exit code (0 if all passed, 1 if any failed)
        """
        # Skip printing in quiet mode (only return exit code)
        if self.config.quiet:
            return 0 if self.tests_failed == 0 else 1

        total = self.tests_passed + self.tests_failed
        total_duration = sum(r.duration for r in self.test_results)

        self.output.print_summary(total, self.tests_passed, self.tests_failed)

        # Print individual test details if we have results
        if self.test_results and self.config.verbose:
            print("\nIndividual Test Results:")
            print("-" * 40)
            for result in self.test_results:
                status = "PASS" if result.passed else "FAIL"
                duration_str = f"{result.duration:.2f}s"
                print(f"{result.test_name:30s} {status:8s} {duration_str}")

        if total_duration > 0:
            print(f"\nTotal Duration: {total_duration:.2f}s")

        return 0 if self.tests_failed == 0 else 1

    def reset(self) -> None:
        """Reset test counters and results.

        Useful for running multiple test suites with the same framework.
        """
        self.tests_passed = 0
        self.tests_failed = 0
        self.test_results.clear()
        self._prerequisites_checked = False

    def __enter__(self):
        """Enter context manager.

        Returns:
            Self
        """
        self.runner.__enter__()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Exit context manager.

        Ensures cleanup even if an exception occurred.

        Args:
            exc_type: Exception type
            exc_val: Exception value
            exc_tb: Exception traceback
        """
        return self.runner.__exit__(exc_type, exc_val, exc_tb)


def create_framework(
    test_name: str = "",
    cpu_count: int = 1,
    timeout: int = 3,
    verbose: bool = False,
    keep_output: bool = False,
    quiet: bool = False
) -> TestFramework:
    """Helper function to create a configured TestFramework.

    Args:
        test_name: Name of the test
        cpu_count: Number of CPUs
        timeout: QEMU timeout in seconds
        verbose: Whether to show verbose output
        keep_output: Whether to keep output files
        quiet: Whether to suppress test start messages

    Returns:
        Configured TestFramework instance
    """
    config = TestConfig(
        test_name=test_name,
        cpu_count=cpu_count,
        qemu_timeout=timeout,
        verbose=verbose,
        keep_output=keep_output,
        quiet=quiet
    )

    return TestFramework(config)


def run_single_test(
    test_name: str,
    cpu_count: int = 1,
    timeout: int = 3,
    verbose: bool = False,
    quiet: bool = False
) -> int:
    """Convenience function to run a single test.

    Args:
        test_name: Name of the test to run
        cpu_count: Number of CPUs to use
        timeout: QEMU timeout in seconds
        verbose: Whether to show verbose output
        quiet: Whether to suppress test start messages

    Returns:
        Exit code (0 if passed, 1 if failed)
    """
    with create_framework(test_name, cpu_count, timeout, verbose, False, quiet) as framework:
        if framework.run_test(test_name, cpu_count):
            return framework.print_summary()
        return 1
