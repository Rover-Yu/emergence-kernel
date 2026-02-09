#!/usr/bin/env python3
"""
QEMU Runner Wrapper for 'make run'

This script wraps the test framework's QEMURunner to provide timeout-based
execution for the 'make run' target. It uses the same QEMU configuration
as the test suite for consistency.

Note: For multiboot ISO, the kernel command line is embedded in the ISO
via grub.cfg, so we use run_qemu_direct() rather than run_qemu_with_env()
which would fail with -append/-cdrom incompatibility.
"""

import argparse
import sys
from pathlib import Path

# Add lib directory to path for imports
lib_dir = Path(__file__).parent / "lib"
sys.path.insert(0, str(lib_dir))

from config import TestConfig
from qemu_runner import QEMURunner


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
        help="Timeout in seconds (default: 8)"
    )
    parser.add_argument(
        "--cpus",
        type=int,
        default=4,
        help="Number of CPUs (default: 4)"
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Enable verbose output"
    )
    parser.add_argument(
        "--quiet",
        action="store_true",
        help="Suppress output"
    )

    args = parser.parse_args()

    # Create test configuration
    config = TestConfig(
        qemu_timeout=args.timeout,
        cpu_count=args.cpus,
        verbose=args.verbose,
        keep_output=False,
    )

    # Create QEMU runner
    runner = QEMURunner(config)

    # Run QEMU directly with timeout
    # Note: Command line is already embedded in the ISO via grub.cfg
    output, exit_code = runner.run_qemu_direct(
        cpu_count=args.cpus,
        timeout=args.timeout
    )

    # Print output to stdout (since we captured it to file)
    if not args.quiet:
        print(output, end="")

    # Return 0 for success (matching original Makefile behavior: || exit 0)
    # Exit code 124 indicates timeout (Python subprocess.TimeoutExpired)
    return 0 if exit_code != 124 else exit_code


if __name__ == "__main__":
    sys.exit(main())
