"""
Configuration Management

Provides dataclasses for test configuration with type safety and validation.
"""

import os
from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Optional


@dataclass
class TestConfig:
    """Configuration for test execution.

    This dataclass encapsulates all configuration options for running tests,
    including paths, QEMU settings, test parameters, and output options.

    Attributes:
        project_root: Root directory of the project (auto-detected)
        kernel_iso: Path to the kernel ISO file (auto-computed)
        qemu_timeout: Timeout in seconds for QEMU execution
        kvm_enabled: Whether KVM acceleration is enabled
        qemu_memory: Memory allocation for QEMU (e.g., "128M")
        qemu_machine: QEMU machine type (e.g., "pc")
        test_name: Name of the test being run
        cpu_count: Number of CPUs to use
        verbose: Whether to show verbose output
        keep_output: Whether to keep test output files
        results_dir: Directory for test results (auto-created)
    """

    # Paths
    project_root: Path = field(default_factory=lambda: Path(__file__).parent.parent.parent)
    kernel_iso: Path = field(init=False)

    # QEMU settings
    qemu_timeout: int = 3
    kvm_enabled: bool = True
    qemu_memory: str = "128M"
    qemu_machine: str = "pc"

    # Test settings
    test_name: str = ""
    cpu_count: int = 1

    # Output settings
    verbose: bool = False
    quiet: bool = False
    keep_output: bool = False
    results_dir: Path = field(init=False)

    def __post_init__(self):
        """Validate and compute derived paths after initialization."""
        # Compute kernel ISO path
        self.kernel_iso = self.project_root / "emergence.iso"

        # Compute and create results directory
        self.results_dir = self.project_root / "tests" / "test_results"
        self.results_dir.mkdir(parents=True, exist_ok=True)

        # Validate cpu_count
        if self.cpu_count < 1:
            raise ValueError(f"cpu_count must be at least 1, got {self.cpu_count}")

        # Validate qemu_timeout
        if self.qemu_timeout < 1:
            raise ValueError(f"qemu_timeout must be at least 1, got {self.qemu_timeout}")

    def get_qemu_cmdline(self, test_name: str) -> str:
        """Get the kernel command line for a test.

        Args:
            test_name: Name of the test

        Returns:
            Kernel command line string
        """
        return f"test={test_name}"

    def get_output_path(self, test_name: str, pid: Optional[int] = None) -> Path:
        """Get the output file path for a test.

        Args:
            test_name: Name of the test
            pid: Process ID (defaults to current PID)

        Returns:
            Path to output file
        """
        if pid is None:
            pid = os.getpid()
        return self.results_dir / f"{test_name}_output_{pid}.txt"


@dataclass
class TestResult:
    """Result of a single test execution.

    Attributes:
        test_name: Name of the test
        passed: Whether the test passed
        exit_code: Exit code from QEMU
        duration: Test execution time in seconds
        message: Optional message about the test result
    """

    test_name: str
    passed: bool
    exit_code: int
    duration: float
    message: str = ""


@dataclass
class TestSuiteSummary:
    """Summary of a test suite run.

    Attributes:
        total_tests: Total number of tests run
        passed: Number of tests that passed
        failed: Number of tests that failed
        results: List of individual test results
        total_duration: Total duration of all tests in seconds
    """

    total_tests: int = 0
    passed: int = 0
    failed: int = 0
    results: List[TestResult] = field(default_factory=list)
    total_duration: float = 0.0

    def add_result(self, result: TestResult) -> None:
        """Add a test result to the summary.

        Args:
            result: TestResult to add
        """
        self.total_tests += 1
        if result.passed:
            self.passed += 1
        else:
            self.failed += 1
        self.results.append(result)
        self.total_duration += result.duration

    @property
    def success_rate(self) -> float:
        """Calculate success rate as a percentage.

        Returns:
            Success rate percentage (0-100)
        """
        if self.total_tests == 0:
            return 0.0
        return (self.passed / self.total_tests) * 100
