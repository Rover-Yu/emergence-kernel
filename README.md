# Emergence Kernel

> "We wrote not a single line of code. We only defined the rules of creation."

> **Emergence Kernel** is an experimental x86_64 operating system kernel written entirely by LLM (Claude) through prompts - **zero hand-written code**.

This is a research kernel project aimed at exploring the boundaries of LLM capabilities in OS kernel development. All code is generated through natural language conversations, including low-level assembly, memory management, and multiprocessor support.

---

## Recent Developments

**Latest: Syscall Framework & User Mode Execution (March 2026)**
- Implemented complete SYSCALL/SYSRET mechanism with ring 0 ↔ ring 3 transitions
- Added syscalls: getpid, yield, fork, wait, write, exit
- Fixed thread context preservation for user mode syscalls (idle thread association)
- Added memory isolation foundation: copy_from_user, process control block, VM management
- Test framework now supports 12 tests with automatic timeout detection
- All tests pass cleanly in `make test` including new syscall test

**Previous: Nested Kernel CR0.WP Toggle (March 2026)**
- Simplified nested kernel architecture using single shared page table
- CR0.WP toggle replaces dual page table CR3 switching for NK/OK transitions
- Eliminates TLB flushes on every monitor call, improving performance
- Updated test framework with new NK trampoline test verification

**Previous: Minilibc & Kernel Relocation (February 2025)**
- Minimal C library for essential string and memory operations
- Kernel relocated to 4MB to avoid GRUB2 reserved memory gap
- 37 comprehensive minilibc tests with Python QEMU automation

See [docs/CHANGELOG.md](docs/CHANGELOG.md) for complete history.

---

## Roadmap

Emergence Kernel is pursuing several ambitious goals to explore novel kernel architectures:

| Goal | Status | Description |
|------|--------|-------------|
| **Basic Kernel Capabilities** | ✅ **In Progress** | Syscalls, process control, threading, memory isolation |
| **Lua Programmable Framework** | Planned | Scriptable kernel extensions via Lua runtime |
| **Graph-Based VFS** | Planned | File system as a graph structure for flexible relationships |
| **Automatic Memory Management** | Planned | Reference counting/GC for kernel objects |
| **LLM-Based File System** | Planned | Semantic storage via specfs or similar |
| **Nested Kernel Architecture** | ✅ **Completed** | Privileged/unprivileged kernel layers with CR0.WP toggle |

📖 See [docs/ROADMAP.md](docs/ROADMAP.md) for the complete roadmap with detailed deliverables.

---

## Building and Running

### Requirements

**Build Tools:**
- GCC (with freestanding support) - C compiler
- GNU binutils (as, ld) - Assembler and linker
- GNU Make - Build system
- GRUB tools (grub-mkrescue) - ISO image creation
- Python 3.8+ - Test framework

**Runtime:**
- QEMU system emulator (x86_64) - For running and testing

### Quick Start

```bash
# Build kernel and ISO (with default configuration)
make

# Run with 2 CPUs (uses Python test framework)
make run

# Run with 4 CPUs
make run CPUS=4

# Run specific test
make run KERNEL_CMDLINE='test=timer'

# Clean build artifacts
make clean
```

### Testing

```bash
# Run all tests (12 tests total)
make test

# Run individual tests
make test-boot          # Basic kernel boot test (1 CPU)
make test-smp           # SMP boot test (2 CPUs)
make test-apic-timer    # APIC timer test (1 CPU)
make test-slab          # Slab allocator test (2 CPUs)
make test-minilibc      # Minilibc string library test (1 CPU)
make test-sched         # Thread scheduler test (2 CPUs)
make test-syscall       # Syscall test (getpid, yield, fork, wait)

# Run specific test via kernel command line
make run KERNEL_CMDLINE='test=timer'     # Run timer test
make run KERNEL_CMDLINE='test=all'       # Run all tests
make run KERNEL_CMDLINE='test=syscall'   # Run syscall tests
```

**Test Coverage:**
- ✅ PMM (Physical Memory Manager)
- ✅ Slab Allocator
- ✅ Thread Scheduler
- ✅ Nested Kernel Trampoline
- ✅ APIC Timer
- ✅ Boot verification
- ✅ SMP startup
- ✅ Page Control Data
- ✅ NK Invariants
- ✅ Read-only Visibility
- ✅ Minilibc (37 tests)
- ✅ Syscall Framework (ring 3 execution)

See [tests/README.md](tests/README.md) for detailed testing documentation.

---

## Project Structure

```
Emergence-Kernel/
├── arch/x86_64/       # Architecture-specific code (boot, APIC, paging, SMP)
├── kernel/              # Architecture-independent kernel (SMP, devices, monitor, PCD)
├── lib/                 # Kernel libraries (minilibc for strings/memory)
├── tests/               # Integration tests and test framework (Python-based QEMU automation)
├── skills/              # Development skills (architecture, build, coding, tests)
├── docs/               # Project documentation (CHANGELOG, design docs, ROADMAP)
├── include/            # Public API headers (string.h, etc.)
├── Makefile            # Build system
├── kernel.config       # Kernel configuration (test/debug features)
├── CLAUDE.md          # Project guide for Claude Code
└── README.md          # This file
```

---

## License

MIT License - see [LICENSE](LICENSE) for details.

---

## References

- [Intel SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html) - x86_64 Architecture Manual
- [OSDev Wiki](https://wiki.osdev.org/) - OS Development Documentation
- [Multiboot2 Specification](https://www.gnu.org/software/grub/manual/multiboot/multiboot.html) - GRUB Boot Protocol
