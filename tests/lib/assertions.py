"""
Pattern Matching Assertions

Provides assertions for verifying test output patterns.
"""

import re
from pathlib import Path
from typing import List, Optional, Pattern


class Assertions:
    """Pattern matching assertions for test verification.

    This class provides methods to check for specific patterns in test
    output, verifying that tests executed correctly.
    """

    def __init__(self, output_content: str):
        """Initialize assertions with output content.

        Args:
            output_content: Content of the test output
        """
        self.content = output_content

    @property
    def content(self) -> str:
        """Get output content.

        Returns:
            Content of the output
        """
        return self._content

    @content.setter
    def content(self, value: str):
        """Set output content.

        Args:
            value: Content to set
        """
        self._content = value

    def assert_test_passed(self, test_name: str) -> bool:
        """Check for [TEST] PASSED: <test_name> marker.

        This is the primary assertion method that looks for the
        standardized test success marker in the kernel output.

        Args:
            test_name: Name of the test to check

        Returns:
            True if marker found, False otherwise
        """
        pattern = rf"\[TEST\] PASSED: {re.escape(test_name)}"
        return bool(re.search(pattern, self.content))

    def assert_test_failed(self, test_name: str) -> bool:
        """Check for [TEST] FAILED: <test_name> marker.

        Args:
            test_name: Name of the test to check

        Returns:
            True if failure marker found, False otherwise
        """
        pattern = rf"\[TEST\] FAILED: {re.escape(test_name)}"
        return bool(re.search(pattern, self.content))

    def assert_pattern_exists(self, pattern: str, flags: int = 0) -> bool:
        """Check if regex pattern exists in output.

        Args:
            pattern: Regular expression pattern to search for
            flags: Optional regex flags (e.g., re.MULTILINE, re.IGNORECASE)

        Returns:
            True if pattern found, False otherwise
        """
        return bool(re.search(pattern, self.content, flags))

    def assert_pattern_count(self, pattern: str, expected: int, flags: int = 0) -> bool:
        """Verify pattern count matches expected.

        Args:
            pattern: Regular expression pattern to count
            expected: Expected number of occurrences
            flags: Optional regex flags

        Returns:
            True if count matches, False otherwise
        """
        actual = len(re.findall(pattern, self.content, flags))
        return actual == expected

    def assert_pattern_at_least(self, pattern: str, minimum: int, flags: int = 0) -> bool:
        """Verify pattern count is at least minimum.

        Args:
            pattern: Regular expression pattern to count
            minimum: Minimum expected count
            flags: Optional regex flags

        Returns:
            True if count >= minimum, False otherwise
        """
        actual = len(re.findall(pattern, self.content, flags))
        return actual >= minimum

    def assert_pattern_between(self, pattern: str, min_count: int, max_count: int, flags: int = 0) -> bool:
        """Verify pattern count is within range.

        Args:
            pattern: Regular expression pattern to count
            min_count: Minimum expected count (inclusive)
            max_count: Maximum expected count (inclusive)
            flags: Optional regex flags

        Returns:
            True if count is within range, False otherwise
        """
        actual = len(re.findall(pattern, self.content, flags))
        return min_count <= actual <= max_count

    def assert_no_exceptions(self) -> bool:
        """Check that no exception characters ('X') are present.

        In the kernel, exception debug characters appear as standalone
        lines containing only A (async), R (ready), or X (exception).
        This method checks for the presence of X characters.

        Returns:
            True if no exceptions, False otherwise
        """
        # Count only standalone exception debug characters
        exception_pattern = r"^[ARX]+$"
        exception_count = 0

        for line in self.content.split('\n'):
            if re.match(exception_pattern, line.strip()):
                exception_count += line.strip().count('X')

        return exception_count == 0

    def assert_string_exists(self, text: str) -> bool:
        """Check if exact string exists in output.

        Args:
            text: String to search for

        Returns:
            True if string found, False otherwise
        """
        return text in self.content

    def assert_string_count(self, text: str, expected: int) -> bool:
        """Verify string count matches expected.

        Args:
            text: String to count
            expected: Expected number of occurrences

        Returns:
            True if count matches, False otherwise
        """
        actual = self.content.count(text)
        return actual == expected

    def get_pattern_matches(self, pattern: str, flags: int = 0) -> List[str]:
        """Get all matches of a pattern.

        Args:
            pattern: Regular expression pattern
            flags: Optional regex flags

        Returns:
            List of matched strings
        """
        return re.findall(pattern, self.content, flags)

    def get_pattern_groups(self, pattern: str, flags: int = 0) -> List[tuple]:
        """Get all capture groups from pattern matches.

        Args:
            pattern: Regular expression pattern with capture groups
            flags: Optional regex flags

        Returns:
            List of tuples containing captured groups
        """
        return re.findall(pattern, self.content, flags)

    def get_failure_snippet(self, lines: int = 30) -> str:
        """Get last N lines of output for debugging.

        Useful for showing context around test failures.

        Args:
            lines: Number of lines to include

        Returns:
            String containing last N lines
        """
        output_lines = self.content.strip().split('\n')
        return '\n'.join(output_lines[-lines:])

    def get_context_around(self, pattern: str, context_lines: int = 5, flags: int = 0) -> List[str]:
        """Get context around pattern matches.

        Args:
            pattern: Pattern to search for
            context_lines: Number of lines before and after
            flags: Optional regex flags

        Returns:
            List of context snippets
        """
        lines = self.content.split('\n')
        contexts = []

        for i, line in enumerate(lines):
            if re.search(pattern, line, flags):
                start = max(0, i - context_lines)
                end = min(len(lines), i + context_lines + 1)
                contexts.append('\n'.join(lines[start:end]))

        return contexts

    def assert_no_panics(self) -> bool:
        """Check that no kernel panics occurred.

        Returns:
            True if no panics, False otherwise
        """
        panic_indicators = [
            r'kernel panic',
            r'BUG:',
            r'panic:',
            r'WARNING:',
            r'Oops:',
            r'general protection fault'
        ]

        for indicator in panic_indicators:
            if re.search(indicator, self.content, re.IGNORECASE):
                return False

        return True

    def assert_not_in_output(self, pattern: str) -> bool:
        """Check that pattern is NOT found in output.

        Args:
            pattern: Regular expression pattern to search for

        Returns:
            True if pattern is NOT found, False otherwise
        """
        return not bool(re.search(pattern, self.content))

    def assert_boot_complete(self) -> bool:
        """Check that kernel boot completed.

        Returns:
            True if boot completed, False otherwise
        """
        boot_markers = [
            r'Kernel boot complete',
            r'boot complete',
            r'Starting kernel',
            r'Kernel command line:'
        ]

        # At least one boot marker should be present
        return any(self.assert_pattern_exists(marker) for marker in boot_markers)

    def get_line_count(self) -> int:
        """Get total line count of output.

        Returns:
            Number of lines in output
        """
        return len(self.content.split('\n'))

    def is_empty(self) -> bool:
        """Check if output is empty.

        Returns:
            True if output is empty, False otherwise
        """
        return not self.content.strip()

    def __repr__(self) -> str:
        """String representation of assertions.

        Returns:
            String showing output file path
        """
        return f"Assertions(output_file={self.output_file})"


class AssertionFailure(Exception):
    """Exception raised when an assertion fails.

    Attributes:
        message: Failure message
        context: Additional context about the failure
    """

    def __init__(self, message: str, context: Optional[str] = None):
        """Initialize assertion failure.

        Args:
            message: Failure message
            context: Optional additional context
        """
        self.message = message
        self.context = context
        super().__init__(self.message)

    def __str__(self) -> str:
        """String representation of failure.

        Returns:
            Formatted failure message
        """
        if self.context:
            return f"{self.message}\nContext: {self.context}"
        return self.message
