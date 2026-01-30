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
├── arch/x86_64/          # Architecture-specific code
│   ├── boot.S           # Boot code (32-bit → 64-bit)
│   ├── ap_trampoline.S  # AP startup trampoline (Real Mode → Long Mode)
│   ├── apic.c           # Local APIC / I/O APIC
│   ├── idt.c            # Interrupt Descriptor Table
│   ├── isr.S            # Interrupt Service Routine stubs
│   ├── vga.c            # VGA text mode driver
│   ├── serial_driver.c  # Serial driver
│   ├── acpi.c           # ACPI parsing
│   ├── timer.c          # Timer framework
│   ├── ipi.c            # Inter-Processor Interrupts
│   ├── monitor/         # Nested Kernel monitor (privileged mode)
│   │   ├── monitor.c     # Monitor core implementation
│   │   ├── monitor.h     # Monitor public interface
│   │   └── monitor_call.S # Assembly stub for CR3/CR0.WP switching
│   ├── paging.h         # x86_64 paging constants
│   └── power.c          # Power management (shutdown)
├── kernel/              # Architecture-independent code
│   ├── main.c           # Kernel main function
│   ├── smp.c            # Multiprocessor support
│   ├── device.c         # Device driver framework
│   ├── monitor/         # Nested Kernel (shared headers)
│   │   └── monitor.h     # Monitor public interface
│   ├── pmm.c            # Physical Memory Manager (buddy system)
│   └── multiboot2.c     # Multiboot2 parsing
├── include/             # Public headers
│   ├── spinlock.h       # Spin lock public interface
│   ├── atomic.h         # Atomic operations
│   ├── barrier.h        # Memory barriers
│   └── smp.h            # SMP interface
├── tests/               # Test code
│   ├── boot/            # Boot integration tests
│   ├── monitor/         # Nested Kernel invariants test
│   ├── smp/             # SMP integration tests
│   ├── timer/           # Timer integration tests
│   ├── spinlock/        # Kernel test code
│   └── lib/             # Test framework library
├── docs/                # Documentation
│   ├── BUILD_CONFIG.md  # Build configuration (English)
│   ├── BUILD_CONFIG_CN.md # Build configuration (中文)
│   ├── atomic.md        # Atomic operations API
│   └── barrier.md       # Memory barrier API
├── skills/              # Claude Code skills
│   ├── architecture/    # Architecture design guidelines
│   ├── build/           # Build requirements
│   ├── coding/          # Code generation guidelines
│   └── tests/           # Test guidelines
├── kernel.config        # Kernel configuration file (default)
├── .config              # Local configuration override (not committed)
├── Makefile             # Build system
├── CLAUDE.md            # Claude Code project guide
├── README.md            # This file (English)
└── README_CN.md         # README (中文)
```

---

## License

MIT License - see [LICENSE](LICENSE) for details.

---

## References

- [Intel SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html) - x86_64 Architecture Manual
- [OSDev Wiki](https://wiki.osdev.org/) - OS Development Documentation
- [Multiboot2 Specification](https://www.gnu.org/software/grub/manual/multiboot/multiboot.html) - GRUB Boot Protocol
