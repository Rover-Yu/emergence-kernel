# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Emergence Kernel is an educational x86_64 operating system kernel written in C and assembly. It implements:
- Multiboot2 boot with GRUB
- Long Mode (64-bit) transition from real mode
- Symmetric Multi-Processing (SMP) with AP startup via trampoline
- Device driver framework with probe/init/remove pattern
- Local APIC, I/O APIC, interrupt handling, and timers
- VGA and serial console output

## Build Commands

### Build kernel and ISO
```bash
make
```

### Clean build artifacts
```bash
make clean
```

### Run in QEMU (2 CPUs)
```bash
make run
# or
./run-qemu.sh  # 4 CPUs
```

### Debug with GDB
Terminal 1:
```bash
make run-debug
```

Terminal 2:
```bash
gdb -x .gdbinit
```

## Architecture

### Directory Structure
- `arch/x86_64/` - Architecture-specific code (boot, APIC, IDT, timers, drivers)
- `kernel/` - Architecture-independent kernel (device framework, SMP, memory management)
- `tests/` - Test suite organized by component (boot, SMP, timer, spinlock)
- `include/` - Currently empty; headers are co-located with sources

### Build System
- Uses a single Makefile with explicit dependency tracking
- AP trampoline (`ap_trampoline.bin.S`) is built as 16-bit binary, then included via `incbin`
- Output: `build/emergence.elf` → `emergence.iso` (multiboot2)

### Boot Flow (BSP - Bootstrap Processor)
1. `_start` in `arch/x86_64/boot.S` (32-bit entry)
2. Atomic increment of `cpu_boot_counter` to determine BSP/AP
3. Setup page tables and GDT for Long Mode
4. Enable Long Mode and jump to 64-bit
5. Copy AP trampoline to physical address 0x7000
6. Call `kernel_main()` in `kernel/main.c`

### AP (Application Processor) Startup
1. BSP sends STARTUP IPI to APs
2. APs start at real mode address 0x7000 (AP trampoline)
3. Trampoline (Real Mode → Protected Mode → Long Mode) in `ap_trampoline.bin.S`
4. Jump to `ap_start()` in `kernel/smp.c`
5. Each CPU gets its own stack (16 KiB)

### Key Subsystems

**SMP (`kernel/smp.c`)**
- Per-CPU info: APIC ID, CPU index, state, stack
- `smp_get_cpu_index()` returns 0 for BSP, 1+ for APs
- CPU states: OFFLINE, BOOTING, ONLINE, READY
- Max CPUs: 2 (configurable in `smp.h`)

**Device Framework (`kernel/device.c`)**
- Linux-inspired probe/init/remove pattern
- Device types: PLATFORM, ISA, PCI, SERIAL, CONSOLE
- Drivers register with `match_id`/`match_mask` for matching
- Devices have `init_priority` for ordering
- Three-phase init: register drivers → probe devices → init devices

**Interrupts (`arch/x86_64/idt.c`, `arch/x86_64/isr.S`)**
- IDT setup with exception handlers
- ISR stubs in assembly, C handlers in idt.c
- Timer interrupts via APIC timer (high-frequency, math quotes)

**APIC (`arch/x86_64/apic.c`)**
- Local APIC (per-CPU) and I/O APIC (interrupt routing)
- IPI (Inter-Processor Interrupt) support for CPU communication

### Memory Layout
- Boot stacks: 16 KiB BSP stack, 16 KiB AP stack area
- AP trampoline loaded at physical 0x7000 during boot
- Page tables identity-map first 2MB

### Current State (as of recent commits)
- AP startup via trampoline is implemented but being debugged
- ACPI parsing temporarily disabled; uses default APIC IDs
- IPI handler EOI fix verified, test not yet implemented

## Testing

### Test Suite Organization

Tests are organized by component in the `tests/` directory:

```
tests/
├── lib/                    # Test framework library
├── boot/                   # Boot integration tests
├── smp/                    # SMP integration tests
├── timer/                  # Timer integration tests
└── spinlock/               # Kernel test code (compiled into kernel)
```

### Running Tests

**Run all tests:**
```bash
make test
# or
make test-all
```

**Run individual tests:**
```bash
make test-boot          # Basic kernel boot test (1 CPU)
make test-smp           # SMP boot test (2 CPUs)
make test-apic-timer    # APIC timer test (1 CPU)
```

### Test Framework

The test suite uses a bash-based framework:
- `tests/lib/test_lib.sh` - Common test utilities (QEMU runner, assertions, output formatting)
- `tests/run_all_tests.sh` - Test suite runner that executes all tests and reports results

Integration tests run QEMU with specified CPU counts, capture serial output, and verify expected patterns in the boot logs.

### Kernel Tests

Kernel tests like `spinlock_test.c` are compiled into the kernel binary and execute during boot. These tests verify synchronization primitives and multi-CPU coordination.

See `tests/README.md` for detailed test documentation.
