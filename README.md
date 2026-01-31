# Emergence Kernel

> "We wrote not a single line of code. We only defined the rules of creation."

> **Emergence Kernel** is an experimental x86_64 operating system kernel written entirely by LLM (Claude) through prompts - **zero hand-written code**.

This is a research kernel project aimed at exploring the boundaries of LLM capabilities in OS kernel development. All code is generated through natural language conversations, including low-level assembly, memory management, and multiprocessor support.

---

## Current Status

| Feature | Status | Notes |
|---------|--------|-------|
| BSP Boot | ✅ Complete | Single-core boot working with banner |
| AP Boot | ✅ Complete | Multi-core boot with state management |
| Interrupt Handling | ✅ Complete | IDT and exception handlers working |
| APIC Init | ✅ Complete | Local APIC and I/O APIC |
| APIC Timer | ✅ Complete | High-frequency timer interrupts with math quotes |
| Device Framework | ✅ Complete | probe/init/remove pattern |
| Physical Memory Manager | ✅ Complete | Buddy system allocator with Multiboot2 |
| Synchronization Primitives | ✅ Complete | Spin locks, RW locks, IRQ-safe locks |
| Test Framework | ✅ Complete | PMM, spin lock, and nested kernel invariants tests |
| ACPI Parsing | ⚠️ Partial | Using default APIC IDs for now |
| **Nested Kernel** | ✅ Complete | All 6 invariants enforced on BSP and APs |
| **Monitor Mode** | ✅ Complete | Privileged/unprivileged page table separation |
| **CR0.WP Protection** | ✅ Complete | Two-level protection (PTE + CR0.WP) |

---

## Building and Running

### Requirements
- GCC (with freestanding support)
- GNU binutils (as, ld)
- GRUB tools (grub-mkrescue)
- QEMU system emulator

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

---

## Project Structure

```
Emergence-Kernel/
├── arch/x86_64/          # x86_64 architecture-specific code (boot, APIC, IDT, timers, drivers, monitor)
├── kernel/               # Architecture-independent kernel code (SMP, device framework, PMM, multiboot2)
├── include/              # Public headers (spinlock, atomic, barrier, SMP interfaces)
├── tests/                # Test suite (boot, SMP, timer, monitor invariants, framework library)
├── docs/                 # API documentation (atomic, barrier, monitor API)
├── skills/               # Claude Code skills (architecture, build, tests guidelines)
├── kernel.config         # Default kernel configuration
├── .config               # Local configuration override (not committed)
├── Makefile              # Build system
├── CLAUDE.md             # Claude Code project guide
└── README.md             # This file
```

---

## License

MIT License - see [LICENSE](LICENSE) for details.

---

## References

- [Intel SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html) - x86_64 Architecture Manual
- [OSDev Wiki](https://wiki.osdev.org/) - OS Development Documentation
- [Multiboot2 Specification](https://www.gnu.org/software/grub/manual/multiboot/multiboot.html) - GRUB Boot Protocol
