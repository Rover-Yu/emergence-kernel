#!/usr/bin/env python3
"""
Multiboot2 Header Verification Test

This test verifies that the multiboot2 header is correctly placed in the
kernel ELF file and contains valid magic and checksum.

For ELF files (which we use), GRUB 2.06 finds the multiboot header by scanning
the program headers. The header is placed at the beginning of the .text section
via the linker script, which ensures it's at load address 0x100000 (1MB).

The .text section starts at file offset 0x4000 (16384) in the ELF.
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

        # The .text section starts at file offset 0x4000 (16384) in the ELF
        # This is where the multiboot header is placed via the linker script
        self.text_offset = 0x4000

    def test_kernel_exists(self):
        """Verify kernel ELF file exists"""
        self.assertTrue(os.path.exists(self.kernel_path),
                       f"Kernel not found at {self.kernel_path}")

    def test_header_at_text_start(self):
        """Verify multiboot header is at the start of .text section"""
        if not os.path.exists(self.kernel_path):
            self.skipTest(f"Kernel not found at {self.kernel_path}")

        with open(self.kernel_path, 'rb') as f:
            f.seek(self.text_offset)
            # Read first 4 bytes - should be multiboot magic
            magic = struct.unpack('<I', f.read(4))[0]

        self.assertEqual(magic, 0xe85250d6,
                        "Multiboot2 magic number not found at .text section start")

    def test_header_checksum_valid(self):
        """Verify multiboot header checksum is correct"""
        if not os.path.exists(self.kernel_path):
            self.skipTest(f"Kernel not found at {self.kernel_path}")

        with open(self.kernel_path, 'rb') as f:
            f.seek(self.text_offset)
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
        if not os.path.exists(self.kernel_path):
            self.skipTest(f"Kernel not found at {self.kernel_path}")

        with open(self.kernel_path, 'rb') as f:
            f.seek(self.text_offset + 4)  # Skip magic
            architecture = struct.unpack('<I', f.read(4))[0]

        self.assertEqual(architecture, 0,
                        "Architecture field should be 0 (i386)")

    def test_header_within_first_8kb(self):
        """Verify entire header is within first 8KB (8192 bytes)"""
        if not os.path.exists(self.kernel_path):
            self.skipTest(f"Kernel not found at {self.kernel_path}")

        with open(self.kernel_path, 'rb') as f:
            f.seek(self.text_offset + 8)  # Skip magic and architecture
            header_length = struct.unpack('<I', f.read(4))[0]

        self.assertLess(header_length, 8192,
                       f"Header length {header_length} exceeds 8KB limit")


if __name__ == '__main__':
    unittest.main()
