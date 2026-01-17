# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

JAKernel is a minimal experimental operating system kernel for x86 architecture. It boots via GRUB using the Multiboot2 protocol and outputs "Hello, JAKernel!" to both VGA display and serial port.

**Important Architecture Note**: Despite the original requirements specifying x86-64, the kernel is actually compiled as 32-bit (i386) for GRUB Multiboot2 compatibility. The QEMU command uses `qemu-system-x86_64` but the kernel itself is 32-bit.

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
2. Multiboot2 header in `boot.S` is validated by GRUB
3. `_start` entry point in `boot.S` sets up the stack and calls `kernel_main`
4. `kernel_main()` in `kernel.c` initializes hardware and outputs messages

### Source Files

| File | Purpose |
|------|---------|
| `src/boot.S` | Multiboot2 header, entry point (_start), stack setup, halt loop |
| `src/kernel.c` | Main kernel logic, VGA output, serial port output |
| `src/linker.ld` | Memory layout: kernel at 1MB, 4KB-aligned sections |

### Memory Layout (defined in linker.ld)
- Kernel loads at physical address 1MB
- Multiboot header must be in first 8KB (required by GRUB)
- All sections (text, rodata, data, bss) are 4KB-aligned
- 16KB stack in `.bss` section

### Hardware Interfaces
- **VGA Text Mode**: Direct write to `0xB8000` (80x25 character grid)
- **Serial Port (COM1)**: I/O ports starting at `0x3F8`, configured for 115200 baud

### Build Notes
- Uses `-m32` flag for 32-bit compilation despite QEMU being x86_64
- Freestanding C (`-nostdlib`, `-ffreestanding`) - no standard library
- Compiled with GCC (`-Wall -Wextra -O2`)
- Linked as ELF32 format (`-m elf_i386`)

## Development Workflow

1. Edit source files in `src/`
2. Run `make all` to build the ISO
3. Run `make run` to test in QEMU
4. Check serial output in terminal for "Hello, JAKernel!"
5. Use `make run-debug` and GDB for more detailed debugging

## QEMU Configuration
- Machine: Q35 chipset
- Memory: 128MB
- Serial output: Redirected to stdio
- Single core (default)
