"""
Emergence Kernel Test Framework

A modular, object-oriented Python 3 test framework for kernel testing.
Provides drop-in replacements for bash test scripts with better error handling,
resource management, and maintainability.
"""

from .test_framework import TestFramework
from .config import TestConfig
from .output import TerminalOutput, ANSI
from .qemu_runner import QEMURunner
from .assertions import Assertions

__all__ = [
    'TestFramework',
    'TestConfig',
    'TerminalOutput',
    'ANSI',
    'QEMURunner',
    'Assertions',
]

__version__ = '1.0.0'
