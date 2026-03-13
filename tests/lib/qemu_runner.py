"""
QEMU Execution with Cleanup

Handles QEMU execution with proper resource cleanup using context managers.
"""

import os
import signal
import subprocess
import sys
import tempfile
import threading
import time
from contextlib import contextmanager
from pathlib import Path
from typing import Callable, List, Optional, Tuple

try:
    from .config import TestConfig
except ImportError:
    from config import TestConfig


class StreamingProcessExecutor:
    """Handles subprocess execution with output streaming.

    Uses non-daemon thread to ensure all output is captured before exit.
    Prevents pipe buffer overflow and data loss.
    """

    CHUNK_SIZE = 65536  # 64KB chunks for efficient reading

    def __init__(self, command: List[str], timeout: int,
                 output_callback: Optional[Callable[[bytes], None]] = None):
        """Initialize the streaming executor.

        Args:
            command: Command to execute
            timeout: Timeout in seconds
            output_callback: Optional callback for real-time output (receives bytes)
        """
        self.command = command
        self.timeout = timeout
        self.output_callback = output_callback
        self.output_bytes: bytearray = bytearray()
        self.output_lock = threading.Lock()
        self.process: Optional[subprocess.Popen] = None
        self.reader_thread: Optional[threading.Thread] = None

    def _reader_thread_func(self, stdout) -> None:
        """Continuously read output until EOF.

        Args:
            stdout: Binary file object to read from
        """
        while True:
            try:
                chunk = stdout.read(self.CHUNK_SIZE)
                if not chunk:  # EOF
                    break
                with self.output_lock:
                    self.output_bytes.extend(chunk)
                if self.output_callback:
                    self.output_callback(chunk)
            except Exception:
                # Pipe closed or error, stop reading
                break

    def run(self) -> Tuple[str, int]:
        """Execute with streaming capture.

        Returns:
            Tuple of (complete_output, exit_code)
        """
        self.process = subprocess.Popen(
            self.command,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            bufsize=0  # Unbuffered binary mode
        )

        # Use non-daemon thread to ensure it completes before exit
        self.reader_thread = threading.Thread(
            target=self._reader_thread_func,
            args=(self.process.stdout,)
        )
        self.reader_thread.start()

        try:
            # Wait for process to complete (naturally or via timeout)
            exit_code = self.process.wait(timeout=self.timeout)
        except subprocess.TimeoutExpired:
            # DON'T kill immediately - give process time to flush output
            # The kernel should exit via isa-debug-exit, but if it doesn't,
            # we'll kill after a short grace period
            try:
                exit_code = self.process.wait(timeout=2.0)
            except subprocess.TimeoutExpired:
                # Process is hung, force kill
                self.process.kill()
                exit_code = 124

        # Wait for reader thread to finish - CRITICAL: wait indefinitely
        # Non-daemon thread will keep running until it reads all data (EOF)
        # No timeout - we MUST wait for thread to finish to capture all output
        self.reader_thread.join()

        # Close stdout
        if self.process.stdout:
            try:
                self.process.stdout.close()
            except Exception:
                pass

        with self.output_lock:
            # Decode bytes to text, using surrogateescape for invalid sequences
            complete_output = self.output_bytes.decode('utf-8', errors='surrogateescape')

        return complete_output, exit_code


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

    def _build_qemu_command_base(self, cpu_count: int) -> List[str]:
        """Build base QEMU command line."""
        cmd = ["qemu-system-x86_64"]

        # Add KVM acceleration if enabled
        if self.config.kvm_enabled:
            cmd.append("-enable-kvm")

        # Add machine and memory settings
        cmd.extend([
            "-M", self.config.qemu_machine,
            "-m", self.config.qemu_memory,
            "-monitor", "none",
            "-no-reboot",  # Exit instead of rebooting on guest reset/shutdown
            "-smp", str(cpu_count),
            "-cdrom", str(self.config.kernel_iso),
            "-device", "isa-debug-exit,iobase=0xB004,iosize=1"
        ])

        return cmd

    def _build_qemu_command_pipe(self, cpu_count: int) -> List[str]:
        """Build QEMU command line with pipe IPC via -nographic.

        The -nographic flag redirects serial output to stdout, which is
        captured by Python subprocess via pipe. No temporary files needed.

        Args:
            cpu_count: Number of CPUs

        Returns:
            List of command arguments
        """
        cmd = self._build_qemu_command_base(cpu_count)

        # Use -nographic for pipe IPC (serial to stdout)
        cmd.extend([
            "-display", "none",
            "-nographic"
        ])

        # Add debug mode flags (-s for GDB server, -S to freeze at startup)
        if self.config.debug_mode:
            cmd.extend(["-s", "-S"])

        return cmd

    def run_via_make(self, cmdline: str, timeout: int) -> Tuple[str, int]:
        """Run test via 'make run KERNEL_CMDLINE=...'.

        This is the preferred method as it uses the project's Makefile
        which may have additional configuration.

        The Makefile's 'run' target calls tests/run.py which uses
        pipe IPC via -nographic for reliable serial output capture.

        Args:
            cmdline: Kernel command line (e.g., "test=boot")
            timeout: Timeout in seconds

        Returns:
            Tuple of (output_content, exit_code)
        """
        cmd = [
            "make", "-C", str(self.config.project_root),
            "run", f"KERNEL_CMDLINE={cmdline}"
        ]

        if self.config.verbose:
            print(f"Running: {' '.join(cmd)}", file=sys.stderr)

        # Real-time output callback - always print chunks as they arrive
        # Use callback=None in quiet mode to suppress real-time output
        callback = None if self.config.quiet else lambda chunk: (sys.stdout.buffer.write(chunk), sys.stdout.buffer.flush())

        executor = StreamingProcessExecutor(
            command=cmd,
            timeout=timeout,
            output_callback=callback
        )

        content, exit_code = executor.run()

        # Save to file for record-keeping
        with self.capture_output("test") as output_file:
            output_file.write_text(content)

        return content, exit_code

    def run_qemu_direct(self, cpu_count: int, timeout: int) -> Tuple[str, int]:
        """Run QEMU directly using pipe IPC for serial output.

        This method bypasses the Makefile and runs QEMU directly.
        Uses -nographic flag which redirects serial to stdout for pipe capture.
        Output is printed in real-time as it arrives from QEMU.

        Args:
            cpu_count: Number of CPUs
            timeout: Timeout in seconds

        Returns:
            Tuple of (output_content, exit_code)
        """
        # Build QEMU command with pipe IPC (-nographic redirects serial to stdout)
        qemu_cmd = self._build_qemu_command_pipe(cpu_count)

        if self.config.verbose:
            print(f"Running: {' '.join(qemu_cmd)}", file=sys.stderr)

        # Real-time output callback - always print chunks as they arrive
        # Use callback=None in quiet mode to suppress real-time output
        callback = None if self.config.quiet else lambda chunk: (sys.stdout.buffer.write(chunk), sys.stdout.buffer.flush())

        executor = StreamingProcessExecutor(
            command=qemu_cmd,
            timeout=timeout,
            output_callback=callback
        )

        content, exit_code = executor.run()

        # Save to output file for record-keeping
        with self.capture_output("qemu") as output_file:
            output_file.write_text(content, errors='replace')

        return content, exit_code

    def run_qemu_with_env(self, cpu_count: int, cmdline: str, timeout: int) -> Tuple[str, int]:
        """Run QEMU with custom kernel command line using pipe IPC.

        Combines direct QEMU execution with a custom command line.
        Uses -nographic flag for pipe-based serial capture.
        Output is printed in real-time as it arrives from QEMU.

        Args:
            cpu_count: Number of CPUs
            cmdline: Kernel command line
            timeout: Timeout in seconds

        Returns:
            Tuple of (output_content, exit_code)
        """
        # Build QEMU command with pipe IPC
        qemu_cmd = self._build_qemu_command_pipe(cpu_count)

        # Add kernel command line via -append
        qemu_cmd.extend(["-append", cmdline])

        if self.config.verbose:
            print(f"Running: {' '.join(qemu_cmd)}", file=sys.stderr)

        # Real-time output callback - always print chunks as they arrive
        # Use callback=None in quiet mode to suppress real-time output
        callback = None if self.config.quiet else lambda chunk: (sys.stdout.buffer.write(chunk), sys.stdout.buffer.flush())

        executor = StreamingProcessExecutor(
            command=qemu_cmd,
            timeout=timeout,
            output_callback=callback
        )

        content, exit_code = executor.run()

        # Save to output file for record-keeping
        with self.capture_output("qemu") as output_file:
            output_file.write_text(content, errors='replace')

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
