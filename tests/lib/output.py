"""
Colored Terminal Output

Provides ANSI color codes and formatted output for test results.
"""

import sys
from enum import Enum
from typing import Optional, TextIO


class ANSI(Enum):
    """ANSI color codes for terminal output.

    These codes provide colored output for better readability and
    visual distinction between different message types.
    """

    RESET = "\033[0m"
    RED = "\033[0;31m"
    GREEN = "\033[0;32m"
    YELLOW = "\033[1;33m"
    BLUE = "\033[0;34m"
    CYAN = "\033[0;36m"
    BOLD = "\033[1m"
    DIM = "\033[2m"

    @classmethod
    def wrap(cls, text: str, color: 'ANSI') -> str:
        """Wrap text with ANSI color codes.

        Args:
            text: Text to wrap
            color: ANSI color to apply

        Returns:
            Text wrapped with color codes
        """
        return f"{color.value}{text}{cls.RESET.value}"

    @classmethod
    def strip(cls, text: str) -> str:
        """Remove ANSI color codes from text.

        Args:
            text: Text potentially containing ANSI codes

        Returns:
            Text with ANSI codes removed
        """
        import re
        ansi_escape = re.compile(r'\033\[[0-9;]*m')
        return ansi_escape.sub('', text)


class TerminalOutput:
    """Handles colored terminal output for test results.

    Provides methods for printing different types of messages with
    appropriate colors and formatting.
    """

    def __init__(self, file: TextIO = sys.stdout, use_colors: Optional[bool] = None):
        """Initialize TerminalOutput.

        Args:
            file: File object to write to (default: stdout)
            use_colors: Whether to use colors (auto-detect if None)
        """
        self.file = file
        if use_colors is None:
            # Auto-detect: disable colors if output is redirected
            self.use_colors = sys.stdout.isatty()
        else:
            self.use_colors = use_colors

    def _write(self, text: str, color: Optional[ANSI] = None) -> None:
        """Write text to output with optional color.

        Args:
            text: Text to write
            color: Optional ANSI color
        """
        if self.use_colors and color:
            self.file.write(ANSI.wrap(text, color))
        else:
            self.file.write(ANSI.strip(text))
        self.file.flush()

    def print_success(self, text: str, indent: int = 0) -> None:
        """Print success message in green.

        Args:
            text: Message to print
            indent: Number of spaces to indent
        """
        prefix = " " * indent
        self._write(f"{prefix}[PASS] {text}\n", ANSI.GREEN)

    def print_failure(self, text: str, details: Optional[str] = None, indent: int = 0) -> None:
        """Print failure message in red.

        Args:
            text: Failure message
            details: Optional additional details
            indent: Number of spaces to indent
        """
        prefix = " " * indent
        self._write(f"{prefix}[FAIL] {text}\n", ANSI.RED)
        if details:
            self._write(f"{prefix}      {details}\n")

    def print_info(self, text: str) -> None:
        """Print info message in blue.

        Args:
            text: Info message
        """
        self._write(f"{text}\n", ANSI.BLUE)

    def print_warning(self, text: str) -> None:
        """Print warning message in yellow.

        Args:
            text: Warning message
        """
        self._write(f"{text}\n", ANSI.YELLOW)

    def print_header(self, text: str, width: int = 40) -> None:
        """Print a section header.

        Args:
            text: Header text
            width: Width of the header line
        """
        line = "=" * width
        self._write(f"{line}\n")
        self._write(f"{text}\n")
        self._write(f"{line}\n")

    def print_subheader(self, text: str, width: int = 40) -> None:
        """Print a subsection header.

        Args:
            text: Header text
            width: Width of the header line
        """
        line = "-" * width
        self._write(f"{text}\n")
        self._write(f"{line}\n")

    def print_test_start(self, test_name: str, cpu_count: int, timeout: int) -> None:
        """Print test start information.

        Args:
            test_name: Name of the test
            cpu_count: Number of CPUs
            timeout: Timeout in seconds
        """
        self._write(f"Running test: {test_name} ({cpu_count} CPU(s), timeout: {timeout}s)\n")

    def print_test_result(self, test_name: str, passed: bool, message: str = "") -> None:
        """Print test result.

        Args:
            test_name: Name of the test
            passed: Whether the test passed
            message: Optional message
        """
        if passed:
            self.print_success(test_name)
        else:
            self.print_failure(test_name, message)

    def print_summary(self, total: int, passed: int, failed: int, width: int = 40) -> None:
        """Print test summary.

        Args:
            total: Total number of tests
            passed: Number of tests passed
            failed: Number of tests failed
            width: Width of separator lines
        """
        line = "=" * width

        self._write(f"\n{line}\n")
        self._write("Test Summary\n")
        self._write(f"{line}\n")
        self._write(f"Total Tests: {total}\n")
        self._write(f"Passed: {passed}\n")
        self._write(f"Failed: {failed}\n")
        self._write("\n")

        if failed == 0:
            self._write(ANSI.wrap("ALL TESTS PASSED!\n", ANSI.GREEN))
            self._write(ANSI.wrap(f"{line}\n", ANSI.GREEN))
        else:
            self._write(ANSI.wrap("SOME TESTS FAILED\n", ANSI.RED))
            self._write(ANSI.wrap(f"{line}\n", ANSI.RED))

    def print_verbose(self, text: str) -> None:
        """Print verbose output.

        Args:
            text: Text to print
        """
        self._write(f"{text}\n", ANSI.DIM)

    def print_error(self, text: str) -> None:
        """Print error message.

        Args:
            text: Error message
        """
        self._write(f"[ERROR] {text}\n", ANSI.RED)


def print_progress(current: int, total: int, test_name: str) -> None:
    """Print test progress indicator.

    Args:
        current: Current test number (1-indexed)
        total: Total number of tests
        test_name: Name of the test
    """
    output = TerminalOutput()
    output._write(f"[{current}/{total}] Running: {test_name}...\n", ANSI.CYAN)
