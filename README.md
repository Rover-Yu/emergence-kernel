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
| **Page Control Data (PCD)** | ✅ Complete | Page type tracking (NK_NORMAL, NK_PGTABLE, OK_NORMAL, etc.) |
| **Nested Kernel** | ✅ Complete | All 6 invariants enforced on BSP and APs |
| **Monitor Mode** | ✅ Complete | Privileged/unprivileged page table separation |
| **CR0.WP Protection** | ✅ Complete | Two-level protection (PTE + CR0.WP) |
| **Read-Only Mappings** | ✅ Complete | Outer kernel can read nested kernel pages at NESTED_KERNEL_RO_BASE |
| **NK Protection Tests** | ✅ Complete | Page fault tests verify write protection |

### Recent Developments

**Nested Kernel Isolation (Latest)**
- Implemented PCD (Page Control Data) system for tracking page types
- All nested kernel stacks marked with `nk_` prefix (NK_NORMAL type)
- All outer kernel stacks marked with `ok_` prefix (OK_NORMAL type)
- Distinguished trampolines: `ok_ap_boot_trampoline` (outer kernel) vs `nk_entry_trampoline` (nested kernel entry)
- Read-only mappings allow outer kernel to inspect nested kernel state without modification
- Page fault protection tests verify unauthorized writes trigger clean shutdown

**Key Design Decisions**
- 4KB page tables for first 2MB region enable fine-grained protection
- Page table pages (NK_PGTABLE) are read-only in unprivileged view
- Nested kernel code/data (NK_NORMAL) writable in monitor, read-only in unprivileged view
- APIC remains accessible from unprivileged mode (per user requirement)

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
│   ├── boot.S           # BSP 16/32-bit entry, Long Mode transition
│   ├── ap_trampoline.S  # AP real mode trampoline (ok_ap_boot_trampoline)
│   ├── monitor/         # Nested kernel monitor
│   │   └── monitor_call.S  # nk_entry_trampoline (CR3 switching stub)
│   ├── paging.h         # Page table entry flags
│   └── ...
├── kernel/               # Architecture-independent kernel code
│   ├── monitor/         # Monitor core implementation
│   │   └── monitor.c    # PCD, RO mappings, invariants enforcement
│   ├── pcd.c            # Page Control Data system
│   ├── smp.c            # SMP support with ok_cpu_stacks
│   └── ...
├── tests/                # Test suite
│   ├── boot/            # Basic kernel boot tests
│   ├── smp/             # SMP boot tests
│   ├── timer/           # APIC timer tests
│   ├── monitor/         # Nested kernel invariants and protection tests
│   ├── nested_kernel_mapping_protection/  # NK protection tests
│   └── lib/             # Test framework library
├── include/              # Public headers
├── kernel.config         # Default kernel configuration
├── .config               # Local configuration override (not committed)
├── Makefile              # Build system (run `make help` for targets)
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
