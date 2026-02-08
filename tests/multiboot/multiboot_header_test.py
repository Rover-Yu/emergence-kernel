#!/usr/bin/env python3
"""
Multiboot2 Header Verification Test

This test verifies that the multiboot2 header is correctly placed at the
beginning of the kernel ELF file and contains valid magic and checksum.
"""

import sys
import os
import struct
import unittest

# Add test library path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'lib'))


class MultibootHeaderTest(unittest.TestCase):
    """Test multiboot2 header structure and placement"""

    def setUp(self):
        """Set up test - determine build directory"""
        self.build_dir = os.path.join(os.path.dirname(__file__), '..', '..', 'build')
        self.kernel_path = os.path.join(self.build_dir, 'emergence.elf')

    def test_header_at_file_start(self):
        """Verify multiboot header is at the beginning of the kernel"""
        # Skip test if kernel doesn't exist
        if not os.path.exists(self.kernel_path):
            self.skipTest(f"Kernel not found at {self.kernel_path}")

        with open(self.kernel_path, 'rb') as f:
            # Read first 4 bytes - should be multiboot magic
            magic = struct.unpack('<I', f.read(4))[0]

        self.assertEqual(magic, 0xe85250d6,
                        "Multiboot2 magic number not found at file start")

    def test_header_checksum_valid(self):
        """Verify multiboot header checksum is correct"""
        # Skip test if kernel doesn't exist
        if not os.path.exists(self.kernel_path):
            self.skipTest(f"Kernel not found at {self.kernel_path}")

        with open(self.kernel_path, 'rb') as f:
            # Read header fields
            magic = struct.unpack('<I', f.read(4))[0]
            architecture = struct.unpack('<I', f.read(4))[0]
            header_length = struct.unpack('<I', f.read(4))[0]
            checksum = struct.unpack('<i', f.read(4))[0]

        # Verify checksum: -(magic + architecture + header_length)
        # Use unsigned arithmetic for the checksum calculation
        expected_checksum = (-(magic + architecture + header_length)) & 0xFFFFFFFF

        self.assertEqual(checksum & 0xFFFFFFFF, expected_checksum,
                        f"Multiboot2 checksum invalid: expected {expected_checksum:#x}, got {checksum & 0xFFFFFFFF:#x}")

    def test_header_architecture_i386(self):
        """Verify architecture field is 0 (i386)"""
        # Skip test if kernel doesn't exist
        if not os.path.exists(self.kernel_path):
            self.skipTest(f"Kernel not found at {self.kernel_path}")

        with open(self.kernel_path, 'rb') as f:
            f.seek(4)  # Skip magic
            architecture = struct.unpack('<I', f.read(4))[0]

        self.assertEqual(architecture, 0,
                        "Architecture field should be 0 (i386)")

    def test_header_within_first_8kb(self):
        """Verify entire header is within first 8KB (8192 bytes)"""
        # Skip test if kernel doesn't exist
        if not os.path.exists(self.kernel_path):
            self.skipTest(f"Kernel not found at {self.kernel_path}")

        with open(self.kernel_path, 'rb') as f:
            # Read header length
            f.seek(8)  # Skip magic and architecture
            header_length = struct.unpack('<I', f.read(4))[0]

        self.assertLess(header_length, 8192,
                       f"Header length {header_length} exceeds 8KB limit")


if __name__ == '__main__':
    unittest.main()
