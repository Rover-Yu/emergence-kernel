# Emergence Kernel

> "We wrote not a single line of code. We only defined the rules of creation."

> **Emergence Kernel** is an experimental x86_64 operating system kernel written entirely by LLM (Claude) through prompts - **zero hand-written code**.

This is a research kernel project aimed at exploring the boundaries of LLM capabilities in OS kernel development. All code is generated through natural language conversations, including low-level assembly, memory management, and multiprocessor support.

---

## Recent Developments

**Nested Kernel Isolation**
- PCD (Page Control Data) system tracks page types for fine-grained protection
- Separation of privileged monitor mode and unprivileged kernel mode
- Read-only mappings allow outer kernel to inspect nested kernel state safely
- Page fault tests verify write protection invariants

**Test Framework**
- Unified test suite with runtime selection via kernel command line
- Tests for PMM, slab allocator, APIC timer, and nested kernel invariants
- Python-based integration tests with QEMU automation

---

## Roadmap

Emergence Kernel is pursuing several ambitious goals to explore novel kernel architectures:

| Goal | Description |
|------|-------------|
| **Basic Kernel Capabilities** | User/kernel multitasking, virtual memory, system calls |
| **Lua Programmable Framework** | Scriptable kernel extensions via Lua runtime |
| **Graph-Based VFS** | File system as a graph structure for flexible relationships |
| **Automatic Memory Management** | Reference counting/GC for kernel objects |
| **LLM-Based File System** | Semantic storage via specfs or similar |
| **Nested Kernel Architecture** | Privileged/unprivileged kernel layers for isolation |

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

# Run (2 CPUs)
make run

# Run (4 CPUs)
./run-qemu.sh

# Clean build artifacts
make clean
```

### Testing

```bash
# Run all tests
make test

# Run individual tests
make test-boot          # Basic kernel boot test (1 CPU)
make test-smp           # SMP boot test (2 CPUs)
make test-apic-timer    # APIC timer test (1 CPU)
make test-slab          # Slab allocator test (2 CPUs)

# Run specific test via kernel command line
make run KERNEL_CMDLINE='test=timer'     # Run timer test
make run KERNEL_CMDLINE='test=all'       # Run all tests
```

See [tests/README.md](tests/README.md) for detailed testing documentation.

---

## Project Structure

```
Emergence-Kernel/
├── arch/x86_64/          # Architecture-specific code (boot, APIC, paging)
├── kernel/               # Core kernel (SMP, devices, monitor, PCD)
├── tests/                # Integration tests and test framework
├── docs/                 # Documentation (ROADMAP.md, design docs)
├── include/              # Public headers
├── Makefile              # Build system
├── kernel.config         # Default configuration
└── CLAUDE.md             # Project guide for Claude Code
```

---

## License

MIT License - see [LICENSE](LICENSE) for details.

---

## References

- [Intel SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html) - x86_64 Architecture Manual
- [OSDev Wiki](https://wiki.osdev.org/) - OS Development Documentation
- [Multiboot2 Specification](https://www.gnu.org/software/grub/manual/multiboot/multiboot.html) - GRUB Boot Protocol
