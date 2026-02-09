# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Emergence Kernel is a research-oriented x86_64 operating system kernel written in C and assembly. It implements:
- Multiboot2 boot with GRUB
- Long Mode (64-bit) transition from real mode
- Symmetric Multi-Processing (SMP) with AP startup via trampoline
- Device driver framework with probe/init/remove pattern
- Local APIC, I/O APIC, interrupt handling, and timers
- Slab allocator for small object allocation (32B - 4KB)
- VGA and serial console output
- **Minilibc** - Minimal C library for kernel string/memory operations

## Build Commands

### Build kernel and ISO
```bash
make
```

### Clean build artifacts
```bash
make clean
```

### Run in QEMU with custom command line
```bash
make run                                      # Default: motto="Learning with Every Boot"
make run KERNEL_CMDLINE='test=timer'          # Run specific test
make run KERNEL_CMDLINE='test=all'            # Run all tests
make run KERNEL_CMDLINE='test=unified'        # Unified test execution
```

**Note:** `make run` now uses the Python test framework (`tests/run.py`) with an 8-second timeout. This prevents hanging QEMU instances. For debugging, use `make run-debug` which starts GDB server on port 1234.

**Note:** The `KERNEL_CMDLINE` variable is embedded into the kernel at build time and used as a fallback when multiboot info is unavailable (e.g., in QEMU). This enables runtime test selection without recompilation.

### Debug with GDB
Terminal 1:
```bash
make run-debug                                # Starts QEMU with GDB server, frozen at startup
```

Terminal 2:
```bash
gdb -x .gdbinit                              # Connects to QEMU GDB server on localhost:1234
```

### QEMU Testing Patterns

**Python subprocess for QEMU (tests/run.py, tests/lib/qemu_runner.py):**
- Use `subprocess.run()` with `timeout=N` for automatic termination
- For file output, use `buffering=0` with binary mode to prevent data loss on timeout
- Avoid `-serial stdio` with `-nographic` (causes terminal interaction artifacts)
- Debug mode: add `-s -S` flags for GDB server (port 1234, freeze at startup)

**Serial port flushing (arch/x86_64/serial_driver.c, arch/x86_64/power.c):**
- Call `serial_flush()` before shutdown to ensure all output is transmitted
- Flush waits for LSR bit 6 (TEMT - Transmitter Empty) which indicates both THR and FIFO are empty
- Without flush, characters in the UART FIFO may be lost when QEMU exits via isa-debug-exit
- Add halt loop immediately after QEMU shutdown port write to prevent execution from continuing

## Architecture

### Directory Structure
- `arch/x86_64/` - Architecture-specific code (boot, APIC, IDT, timers, drivers)
- `kernel/` - Architecture-independent kernel (device framework, SMP, memory management, slab allocator)
- `lib/minilibc/` - Minimal C library for kernel string/memory operations
- `include/` - Public API headers (string.h for minilibc)
- `tests/` - Test suite organized by component (boot, SMP, timer, spinlock, slab, minilibc)

### Build System
- Uses a single Makefile with explicit dependency tracking
- AP trampoline (`ap_trampoline.bin.S`) is built as 16-bit binary, then included via `incbin`
- Output: `build/emergence.elf` → `emergence.iso` (multiboot2)
- **Embedded command line:** `KERNEL_CMDLINE` is compiled into `build/cmdline_source.c` as fallback when multiboot info is unavailable

### Build System Insights

**Critical Makefile Ordering:**
When using conditional blocks to add objects to a variable (e.g., `TESTS_OBJS`), the conditionals must be evaluated BEFORE the variable is used in other definitions. For example:

```makefile
# WRONG: OBJS defined before conditionals add to TESTS_OBJS
TESTS_OBJS := $(patsubst ...)
OBJS := $(ARCH_OBJS) $(TESTS_OBJS)  # TESTS_OBJS is empty here!
ifeq ($(CONFIG_FOO),1)
TESTS_OBJS += foo.o  # Too late, OBJS already defined
endif

# RIGHT: Conditionals before OBJS definition
TESTS_OBJS := $(patsubst ...)
ifeq ($(CONFIG_FOO),1)
TESTS_OBJS += foo.o  # Added before OBJS uses TESTS_OBJS
endif
OBJS := $(ARCH_OBJS) $(TESTS_OBJS)
```

**Command Line Propagation:**
- `KERNEL_CMDLINE` variable in Makefile → embedded into kernel binary → fallback if multiboot fails
- GRUB is also configured with the command line via grub.cfg generation
- This dual approach ensures command line works in both QEMU (embedded) and real hardware (multiboot)

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

**Slab Allocator (`kernel/slab.c`, `kernel/slab.h`)**
- Linux-inspired slab allocator for efficient small object allocation
- 8 power-of-two caches: 32B, 64B, 128B, 256B, 512B, 1KB, 2KB, 4KB
- Three-list slab management (full/partial/free) for O(1) allocation
- Interrupt-safe via `spin_lock_irqsave()`/`spin_unlock_irqrestore()`
- Foundation for future `kmalloc()`/`kfree()` implementation
- API: `slab_alloc()`, `slab_free()`, `slab_alloc_size()`, `slab_free_size()`

**Minilibc (`lib/minilibc/string.c`, `include/string.h`)**
- Minimal C library for kernel-space string and memory operations
- Stack-only allocation for kernel safety (no heap dependencies)
- Implemented functions: `strlen`, `strcpy`, `strcmp`, `strncmp`, `memset`, `memcpy`
- Comprehensive test coverage: 37 kernel tests covering edge cases
- Python integration test with QEMU framework
- Configuration: `CONFIG_MINILIBC_TESTS` in kernel.config (default: 1)

### Memory Layout
- Boot stacks: 16 KiB BSP stack, 16 KiB AP stack area
- AP trampoline loaded at physical 0x7000 during boot
- Page tables identity-map first 2MB
- Slab allocator: Each slab is one page (4KB), objects packed after metadata header

### Current State (as of recent commits)
- **Unified test framework** with runtime selection via kernel command line
- **APIC timer test** implemented and integrated (runs after APs are ready)
- **Kernel command line support** with embedded fallback for QEMU environments
- **Minilibc** - Minimal string library with 6 functions and 37 comprehensive tests
- Slab allocator with 8 power-of-two caches (32B - 4KB)
- Tests available: PMM, SLAB, Minilibc, APIC Timer, Spinlock (conditional), NK Protection (manual)
- AP startup via trampoline fully functional (3/3 APs boot successfully)
- ACPI parsing temporarily disabled; uses default APIC IDs
- Fail-fast test behavior: system shutdown on first test failure

## Testing

### Test Suite Organization

Tests are organized by component in the `tests/` directory:

```
tests/
├── lib/                    # Test framework library
├── boot/                   # Boot integration tests
├── smp/                    # SMP integration tests
├── timer/                  # Timer integration tests
├── slab/                   # Slab allocator integration tests
├── minilibc/               # Minilibc integration tests
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
make test-slab          # Slab allocator test (2 CPUs)
make test-minilibc      # Minilibc string/memory functions test
```

### Test Framework

The project uses a dual-layer test framework:

**1. Bash Integration Tests (tests/):**
- `tests/lib/test_lib.sh` - Common test utilities (QEMU runner, assertions, output formatting)
- `tests/run_all_tests.sh` - Test suite runner that executes all tests and reports results
- Integration tests run QEMU with specified CPU counts, capture serial output, and verify expected patterns

**2. Unified Kernel Test Framework (kernel/test.c):**
- Runtime test selection via kernel command line: `test=<name|all|unified>`
- Test registry with `test_case_t` structures (name, description, run_func, enabled, auto_run)
- Distributed auto-run: Tests execute at their subsystem init points (when selected)
- Fail-fast behavior: System shuts down immediately on first test failure
- No `test=` parameter: No tests run (default production behavior)

**Available kernel tests:**
- `pmm` - Physical memory manager allocation tests
- `slab` - Slab allocator small object allocation tests
- `minilibc` - String/memory functions tests (strlen, strcpy, strcmp, strncmp, memset, memcpy)
- `timer` - APIC timer interrupt-driven tests (runs after APs are ready)
- `spinlock` - Spinlock synchronization tests (when CONFIG_SPINLOCK_TESTS=1)
- `nk_protection` - Nested kernel mappings protection tests (manual only)

### Kernel Tests

Kernel tests like `spinlock_test.c` are compiled into the kernel binary and execute during boot. These tests verify synchronization primitives and multi-CPU coordination.

See `tests/README.md` for detailed test documentation.
