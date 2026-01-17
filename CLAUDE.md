# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

JAKernel is a minimal experimental operating system kernel for x86-64 architecture. It boots via GRUB using the Multiboot2 protocol and outputs "Hello, JAKernel!" to both VGA display and serial port.

## Build Commands

```bash
# Build the kernel ISO image
make all

# Run the kernel in QEMU (serial output goes to stdout)
make run

# Run the kernel in QEMU with GDB debugging (listens on localhost:1234)
make run-debug

# Clean all build artifacts
make clean
```

## Debugging

When using `make run-debug`, GDB will connect to QEMU's remote debugger on port 1234. The `.gdbinit` file is configured to:
- Connect to `localhost:1234`
- Load symbols from `build/jakernel.elf`
- Set a breakpoint at `kernel_main`
- Continue execution

To debug manually:
```bash
# Terminal 1: Start QEMU with debugging
make run-debug

# Terminal 2: Run GDB (it will auto-load .gdbinit)
gdb
```

## Architecture

### Boot Sequence
1. GRUB loads the kernel ELF file from the ISO
2. Multiboot2 header in `arch/x86_64/boot.S` is validated by GRUB
3. `_start` entry point in `boot.S` switches to 64-bit long mode
4. `long_mode_start` sets up 64-bit environment and calls `kernel_main`
5. `kernel_main()` in `kernel/main.c` initializes hardware and outputs messages

### Directory Structure

```
arch/x86_64/          # Architecture-specific code (x86-64)
├── boot.S            # Boot code with MMU initialization
├── linker.ld         # Linker script for x86-64
├── vga.c             # VGA text mode driver
├── vga.h             # VGA interface
├── serial.c          # Serial port (COM1) driver
├── serial.h          # Serial interface
└── io.h              # I/O port operations (inline asm)

kernel/               # Architecture-independent kernel code
└── main.c            # Main kernel entry point

include/              # Common headers (reserved for future use)
```

### Source File Locations

| Path | Purpose |
|------|---------|
| `arch/x86_64/boot.S` | Multiboot2 header, long mode setup, MMU initialization |
| `arch/x86_64/linker.ld` | Memory layout: kernel at 1MB, 4KB-aligned sections |
| `arch/x86_64/vga.c` | VGA text mode driver |
| `arch/x86_64/serial.c` | Serial port driver |
| `arch/x86_64/io.h` | x86 I/O port operations |
| `kernel/main.c` | Architecture-independent kernel main |

### Memory Layout (defined in linker.ld)
- Kernel loads at physical address 1MB
- Multiboot header must be in first 8KB (required by GRUB)
- All sections (text, rodata, data, bss) are 4KB-aligned
- 16KB stack in `.bss` section

### Hardware Interfaces
- **VGA Text Mode**: Direct write to `0xB8000` (80x25 character grid)
- **Serial Port (COM1)**: I/O ports starting at `0x3F8`, configured for 115200 baud

### Build Notes
- Uses 64-bit x86_64 compilation with `-mcmodel=large` for kernel code
- Freestanding C (`-nostdlib`, `-ffreestanding`) - no standard library
- Compiled with GCC (`-Wall -Wextra -O2`)
- Additional flags: `-mno-red-zone`, `-mno-mmx`, `-mno-sse`, `-mno-sse2`
- Linked as ELF64 format (`-m elf_x86_64`)

## Development Workflow

1. Edit source files in appropriate directories:
   - Architecture-specific code → `arch/x86_64/`
   - Architecture-independent code → `kernel/`
   - Common headers → `include/`
2. Run `make all` to build the ISO
3. Run `make run` to test in QEMU
4. Check serial output in terminal for "Hello, JAKernel!"
5. Use `make run-debug` and GDB for more detailed debugging

## Coding Standards

### Code Organization Rules

This project follows a strict separation between architecture-specific and architecture-independent code:

#### 1. Directory Structure

```
arch/<architecture>/    # Architecture-specific code
kernel/                  # Architecture-independent kernel code
include/                 # Common headers shared across architectures
```

#### 2. What Goes Where

**`arch/<architecture>/`** - Architecture-specific code:
- Boot code and early initialization (`boot.S`, `boot.c`)
- Linker scripts (`linker.ld`)
- Hardware-specific drivers:
  - VGA/text mode display
  - Serial port communication
  - I/O port operations
  - Memory-mapped I/O
  - CPU-specific features (MSR, CR registers, etc.)
- Assembly code for architecture-specific operations
- Interrupt handling and IDT setup
- GDT/TLB management

**`kernel/`** - Architecture-independent code:
- Main kernel entry point
- Generic algorithms and data structures
- Memory management abstractions
- Process management (when implemented)
- File system abstractions (when implemented)
- Generic device interfaces

**`include/`** - Common headers:
- Type definitions (`stdint.h`, `stddef.h` replacements if needed)
- Common macros
- Architecture-independent interfaces
- Configuration constants

#### 3. File Naming Conventions

- C source files: `.c` extension (e.g., `vga.c`, `main.c`)
- Assembly files: `.S` extension (e.g., `boot.S`)
- Header files: `.h` extension (e.g., `vga.h`, `io.h`)
- Linker scripts: `.ld` extension (e.g., `linker.ld`)

#### 4. Header Include Paths

When including headers, use the full path from the project root:
```c
#include "arch/x86_64/io.h"      // Architecture-specific header
#include "arch/x86_64/vga.h"     // Architecture-specific header
#include "kernel/types.h"        // Kernel header (if created)
```

#### 5. Makefile Integration

When adding new files:
- Add architecture-specific C files to `ARCH_C_SRCS` in Makefile
- Add kernel C files to `KERNEL_C_SRCS` in Makefile
- Object files will be automatically generated with appropriate prefixes:
  - `arch/<architecture>/*.c` → `build/arch_*.o`
  - `kernel/*.c` → `build/kernel_*.o`

#### 6. Adding Support for New Architectures

To add a new architecture (e.g., ARM64):
1. Create `arch/arm64/` directory
2. Add architecture-specific boot code, linker script, and drivers
3. Update Makefile to support multiple architectures
4. Ensure `kernel/` code remains architecture-independent
5. Use conditional compilation or function pointers for architecture interfaces

### Code Style

- Use C-style comments (`/* */`) for multi-line explanations
- Use C++-style comments (`//`) for single-line notes
- Use inline assembly with `asm volatile ()` for hardware access
- Use `static inline` for small, performance-critical functions
- Use meaningful variable and function names
- Add comments explaining non-obvious operations, especially for hardware manipulation

## QEMU Configuration
- Machine: Q35 chipset
- Memory: 128MB
- Serial output: Redirected to stdio
- Single core (default)
