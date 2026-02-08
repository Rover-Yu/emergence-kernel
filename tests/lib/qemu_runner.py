"""
QEMU Execution with Cleanup

Handles QEMU execution with proper resource cleanup using context managers.
"""

import os
import signal
import subprocess
import sys
import tempfile
from contextlib import contextmanager
from pathlib import Path
from typing import List, Optional, Tuple

try:
    from .config import TestConfig
except ImportError:
    from config import TestConfig


class QEMURunner:
    """Handles QEMU execution with proper cleanup.

    This class manages QEMU process execution with guaranteed cleanup of
    temporary resources using Python context managers.
    """

    def __init__(self, config: TestConfig):
        """Initialize QEMU runner.

        Args:
            config: Test configuration
        """
        self.config = config
        self._temp_files: List[Path] = []
        self._original_sigint = None
        self._original_sigterm = None

    def _cleanup_file(self, path: Path) -> None:
        """Safely remove a file if it exists.

        Args:
            path: Path to file to remove
        """
        try:
            if path.exists():
                path.unlink()
        except (OSError, IOError) as e:
            # Silently fail - best effort cleanup
            pass

    def cleanup_all(self) -> None:
        """Clean up all temporary files."""
        for temp_file in self._temp_files[:]:  # Copy list to avoid modification during iteration
            self._cleanup_file(temp_file)
        self._temp_files.clear()

    @contextmanager
    def capture_output(self, name: str) -> Path:
        """Context manager for capturing output to a temp file.

        Ensures cleanup even if an exception occurs. The file is tracked
        for cleanup but can be kept if keep_output is True.

        Args:
            name: Name prefix for the output file

        Yields:
            Path to the output file

        Example:
            >>> with runner.capture_output("test") as output_file:
            ...     # Write to output_file
            ...     pass
            # File automatically cleaned up on exit (unless keep_output=True)
        """
        output_file = self.config.get_output_path(name)

        # Track for cleanup
        self._temp_files.append(output_file)

        try:
            yield output_file
        finally:
            # Cleanup only if not keeping output
            if not self.config.keep_output:
                self._cleanup_file(output_file)
                # Remove from tracking list
                if output_file in self._temp_files:
                    self._temp_files.remove(output_file)

    def _build_qemu_command(self, cpu_count: int) -> List[str]:
        """Build QEMU command line.

        Args:
            cpu_count: Number of CPUs

        Returns:
            List of command arguments
        """
        cmd = ["qemu-system-x86_64"]

        # Add KVM acceleration if enabled
        if self.config.kvm_enabled:
            cmd.append("-enable-kvm")

        # Add machine and memory settings
        cmd.extend([
            "-M", self.config.qemu_machine,
            "-m", self.config.qemu_memory,
            "-nographic",
            "-monitor", "none",
            "-smp", str(cpu_count),
            "-cdrom", str(self.config.kernel_iso),
            "-device", "isa-debug-exit,iobase=0xB004,iosize=1",
            "-serial", "stdio"
        ])

        return cmd

    def run_via_make(self, cmdline: str, timeout: int) -> Tuple[str, int]:
        """Run test via 'make run KERNEL_CMDLINE=...'.

        This is the preferred method as it uses the project's Makefile
        which may have additional configuration.

        Args:
            cmdline: Kernel command line (e.g., "test=boot")
            timeout: Timeout in seconds

        Returns:
            Tuple of (output_content, exit_code)
        """
        with self.capture_output("test") as output_file:
            cmd = [
                "make", "-C", str(self.config.project_root),
                "run", f"KERNEL_CMDLINE={cmdline}"
            ]

            if self.config.verbose:
                print(f"Running: {' '.join(cmd)}")

            # Use Python's built-in timeout instead of external timeout command
            # to avoid buffering issues where output can be lost when timeout kills process
            try:
                result = subprocess.run(
                    cmd,
                    stdout=output_file.open('w'),
                    stderr=subprocess.STDOUT,
                    timeout=timeout
                )
                exit_code = result.returncode
            except subprocess.TimeoutExpired:
                # Timeout occurred - QEMU was terminated
                exit_code = 124  # Standard timeout exit code

            # Read content before cleanup
            content = output_file.read_text()

            return content, exit_code

    def run_qemu_direct(self, cpu_count: int, timeout: int) -> Tuple[str, int]:
        """Run QEMU directly (legacy method for compatibility).

        This method bypasses the Makefile and runs QEMU directly.
        Useful for testing or when Makefile integration is not desired.

        Args:
            cpu_count: Number of CPUs
            timeout: Timeout in seconds

        Returns:
            Tuple of (output_content, exit_code)
        """
        with self.capture_output("qemu") as output_file:
            qemu_cmd = self._build_qemu_command(cpu_count)

            if self.config.verbose:
                print(f"Running: {' '.join(qemu_cmd)}")

            # Use Python's built-in timeout instead of external timeout command
            # to avoid buffering issues where output can be lost when timeout kills process
            try:
                result = subprocess.run(
                    qemu_cmd,
                    stdout=output_file.open('w'),
                    stderr=subprocess.STDOUT,
                    timeout=timeout
                )
                exit_code = result.returncode
            except subprocess.TimeoutExpired:
                # Timeout occurred - QEMU was terminated
                exit_code = 124  # Standard timeout exit code

            # Read content before cleanup
            content = output_file.read_text()

            return content, exit_code

    def run_qemu_with_env(self, cpu_count: int, cmdline: str, timeout: int) -> Tuple[str, int]:
        """Run QEMU with custom kernel command line.

        Combines direct QEMU execution with a custom command line.

        Args:
            cpu_count: Number of CPUs
            cmdline: Kernel command line
            timeout: Timeout in seconds

        Returns:
            Tuple of (output_content, exit_code)
        """
        with self.capture_output("qemu") as output_file:
            qemu_cmd = self._build_qemu_command(cpu_count)

            # Add kernel command line
            qemu_cmd.extend(["-append", cmdline])

            if self.config.verbose:
                print(f"Running: {' '.join(qemu_cmd)}")

            # Use Python's built-in timeout instead of external timeout command
            # to avoid buffering issues where output can be lost when timeout kills process
            try:
                result = subprocess.run(
                    qemu_cmd,
                    stdout=output_file.open('w'),
                    stderr=subprocess.STDOUT,
                    timeout=timeout
                )
                exit_code = result.returncode
            except subprocess.TimeoutExpired:
                # Timeout occurred - QEMU was terminated
                exit_code = 124  # Standard timeout exit code

            # Read content before cleanup
            content = output_file.read_text()

            return content, exit_code

    def __enter__(self):
        """Enter context manager.

        Returns:
            Self
        """
        # Store original signal handlers
        self._original_sigint = signal.getsignal(signal.SIGINT)
        self._original_sigterm = signal.getsignal(signal.SIGTERM)

        # Set up cleanup handlers
        def cleanup_handler(signum, frame):
            self.cleanup_all()
            # Restore original handlers and re-raise
            if self._original_sigint:
                signal.signal(signal.SIGINT, self._original_sigint)
            if self._original_sigterm:
                signal.signal(signal.SIGTERM, self._original_sigterm)
            sys.exit(1)

        signal.signal(signal.SIGINT, cleanup_handler)
        signal.signal(signal.SIGTERM, cleanup_handler)

        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Exit context manager.

        Ensures cleanup even if an exception occurred.

        Args:
            exc_type: Exception type
            exc_val: Exception value
            exc_tb: Exception traceback
        """
        self.cleanup_all()

        # Restore original signal handlers
        if self._original_sigint:
            signal.signal(signal.SIGINT, self._original_sigint)
        if self._original_sigterm:
            signal.signal(signal.SIGTERM, self._original_sigterm)

        return False  # Don't suppress exceptions


def find_qemu_binary() -> Optional[Path]:
    """Find the QEMU binary in the system PATH.

    Returns:
        Path to QEMU binary if found, None otherwise
    """
    import shutil

    qemu_path = shutil.which("qemu-system-x86_64")
    if qemu_path:
        return Path(qemu_path)
    return None


def check_qemu_version(min_version: Tuple[int, int, int] = (2, 0, 0)) -> bool:
    """Check if QEMU version meets minimum requirements.

    Args:
        min_version: Minimum required version as (major, minor, patch)

    Returns:
        True if QEMU version is sufficient, False otherwise
    """
    import re

    try:
        result = subprocess.run(
            ["qemu-system-x86_64", "--version"],
            capture_output=True,
            text=True,
            timeout=5
        )

        # Parse version from output like "QEMU emulator version 7.2.0"
        match = re.search(r'version (\d+)\.(\d+)\.(\d+)', result.stdout)
        if match:
            major, minor, patch = map(int, match.groups())
            return (major, minor, patch) >= min_version

    except (subprocess.TimeoutExpired, FileNotFoundError, ValueError):
        pass

    return False
