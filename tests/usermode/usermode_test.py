#!/usr/bin/env python3
"""
User Mode Syscall Test

Tests the SYSCALL/SYSRET mechanism for user mode system calls.
Verifies that the kernel can:
- Set up SYSCALL MSRs (IA32_EFER.SCE, STAR, LSTAR, FMASK)
- Execute SYSCALL from ring 0
- Transition to ring 3 using sysretq
- Handle syscalls from ring 3

Note: Ring 3 transition via sysretq has known issues in QEMU TCG mode.
This test is expected to work on real hardware or with KVM.
"""

import sys
import argparse
from pathlib import Path

# Add lib directory to path for imports
lib_path = Path(__file__).parent.parent / "lib"
sys.path.insert(0, str(lib_path))

from test_framework import TestFramework, TestConfig, create_framework
from output import TerminalOutput, ANSI


def parse_arguments():
    """Parse command line arguments."""
    parser = argparse.ArgumentParser(
        description="User Mode Syscall Test",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s              Run with default settings
  %(prog)s --verbose    Show detailed output
  %(prog)s --kvm        Use KVM acceleration (recommended)

Note: This test may hang in QEMU TCG mode during ring 3 transition.
Use --kvm or run on real hardware for full functionality.
        """
    )
    parser.add_argument(
        "-v", "--verbose",
        action="store_true",
        help="Show detailed test output"
    )
    parser.add_argument(
        "--keep-output",
        action="store_true",
        help="Keep test output files for debugging"
    )
    parser.add_argument(
        "--timeout",
        type=int,
        default=3,
        metavar="SECONDS",
        help="QEMU timeout in seconds (default: 3)"
    )
    parser.add_argument(
        "--kvm",
        action="store_true",
        help="Use KVM acceleration (recommended for this test)"
    )
    return parser.parse_args()


def main():
    """Main test execution."""
    args = parse_arguments()

    output = TerminalOutput()

    # Print test header
    output.print_header("User Mode Syscall Test", width=50)
    print(f"CPU Count: 1")
    print(f"Timeout: {args.timeout} seconds")
    print(f"KVM: {'Enabled' if args.kvm else 'Disabled (TCG mode)'}")
    print()

    # Show warning about TCG mode limitations
    if not args.kvm:
        output.print_warning("Note: Ring 3 transition may hang in QEMU TCG mode")
        output.print_warning("Consider using --kvm for full testing")
        print()

    # Create framework and run test
    framework = create_framework(
        test_name="usermode",
        cpu_count=1,
        timeout=args.timeout,
        verbose=args.verbose,
        keep_output=args.keep_output
    )

    # Set KVM mode if requested
    if args.kvm:
        framework.config.kvm_enabled = True

    # Check prerequisites
    if not framework.check_prerequisites():
        output.print_error("Prerequisites not met")
        sys.exit(1)

    # Run the test
    print("Starting QEMU with 1 CPU...")
    print()

    if framework.run_test("usermode", cpu_count=1):
        exit_code = framework.print_summary()
    else:
        exit_code = 1

    sys.exit(exit_code)


if __name__ == "__main__":
    main()
