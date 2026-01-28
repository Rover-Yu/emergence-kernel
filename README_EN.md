# Emergence Kernel

> **Emergence Kernel** is an experimental x86_64 operating system kernel written entirely by LLM (Claude) through prompts - **zero hand-written code**.

This is a research kernel project aimed at exploring the boundaries of LLM capabilities in OS kernel development. All code is generated through natural language conversations, including low-level assembly, memory management, and multiprocessor support.

---

## Features

### Currently Implemented

#### Boot and Mode Transitions
- **Multiboot2 Boot** - Kernel loaded via GRUB with memory map parsing
- **Real Mode → Protected Mode → Long Mode** - Complete mode transition flow
- **Page Table Setup** - Identity mapping for first 2MB
- **GDT Configuration** - 64-bit Global Descriptor Table
- **Boot Banner** - Visual kernel identification on startup

#### Symmetric Multi-Processing (SMP)
- **BSP/AP Detection** - Distinguish Bootstrap vs Application Processors via atomic counter
- **AP Startup Trampoline** - Jump from real mode to long mode
- **IPI Communication** - Inter-Processor Interrupt support
- **Per-CPU Stack** - Independent stack space (16 KiB each)
- **CPU State Management** - OFFLINE, BOOTING, ONLINE, READY states

#### Synchronization Primitives
- **Spin Locks** - Basic spin locks with `spin_lock`/`spin_unlock`
- **IRQ-Safe Locks** - `spin_lock_irqsave`/`spin_unlock_irqrestore` for interrupt context
- **Read-Write Locks** - Multiple readers, exclusive writer access
- **Comprehensive Testing** - 10 tests including single-CPU and SMP multi-CPU scenarios

#### Physical Memory Management
- **Buddy System Allocator** - Efficient power-of-2 block allocation
- **Multiboot2 Memory Map** - Parses available/reserved memory regions
- **Order-Based Allocation** - Supports orders 0-9 (4KB to 2MB blocks)
- **Automatic Coalescing** - Adjacent free blocks are merged
- **Statistics Tracking** - Free and total page counts

#### Interrupt and Exception Handling
- **IDT Setup** - Complete Interrupt Descriptor Table
- **Exception Handlers** - Divide by zero, page fault, etc.
- **ISR Stubs** - Assembly interrupt service routines
- **APIC Timer** - High-frequency timer interrupts (RTC removed due to QEMU resets)

#### Device Driver Framework
- **Linux-style Driver Model** - probe/init/remove pattern
- **Device Types** - PLATFORM, ISA, PCI, SERIAL, CONSOLE
- **Priority-based Init** - Support for driver initialization ordering
- **Matching Mechanism** - match_id/match_mask device matching

#### APIC (Advanced Programmable Interrupt Controller)
- **Local APIC** - Per-CPU interrupt controller initialization
- **I/O APIC** - External interrupt routing
- **IPI Support** - Inter-processor interrupt send/receive
- **Timer Support** - APIC timer for scheduling and timekeeping

#### Console Output
- **VGA Text Mode** - 80x25 character display
- **Serial Output** (COM1) - Debug information output
- **Standardized Logging** - Consistent log prefixes across all subsystems

#### Power Management
- **System Shutdown** - Clean shutdown after SMP initialization and tests

---

## Building and Running

### Requirements
- GCC (with freestanding support)
- GNU binutils (as, ld)
- GRUB tools (grub-mkrescue)
- QEMU system emulator

### Quick Start

```bash
# Build kernel and ISO
make

# Run (2 CPUs)
make run

# Run (4 CPUs)
./run-qemu.sh

# Clean build artifacts
make clean
```

### GDB Debugging

Terminal 1:
```bash
make run-debug
```

Terminal 2:
```bash
gdb -x .gdbinit
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
│   ├── power.c          # Power management (shutdown)
│   └── spinlock_arch.h  # x86_64 spin lock implementation
├── kernel/              # Architecture-independent code
│   ├── main.c           # Kernel main function
│   ├── smp.c            # Multiprocessor support
│   ├── device.c         # Device driver framework
│   ├── pmm.c            # Physical Memory Manager (buddy system)
│   ├── multiboot2.c     # Multiboot2 parsing
│   ├── list.h           # Doubly-linked list
│   └── spinlock_test.c  # Spin lock test suite
├── include/             # Public headers
│   └── spinlock.h       # Spin lock public interface
├── tests/               # Test code
│   └── timer_test.c     # Timer tests
└── Makefile             # Build system
```

---

## Boot Flow

### BSP (Bootstrap Processor) Flow

1. **`_start`** (arch/x86_64/boot.S) - 32-bit entry point
2. **BSP Detection** - Atomic increment of `cpu_boot_counter`
3. **Page Table Setup** - Configure 4-level page tables
4. **GDT Load** - Setup 64-bit Global Descriptor Table
5. **Enable Long Mode** - Set CR4.PAE, IA32_EFER.LME, CR0.PG
6. **Jump to 64-bit** - Far jump to kernel_main()
7. **AP Startup** - Copy trampoline to 0x7000, send STARTUP IPI

### AP (Application Processor) Flow

1. **STARTUP IPI** - Receive startup vector
2. **Real Mode** - Begin execution at 0x7000
3. **Protected Mode** - Load GDT32
4. **Long Mode** - Enable PAE, load CR3, enable paging
5. **C Code** - Jump to `ap_start()`
6. **Initialization Complete** - Output "[AP] CPU X initialized successfully"

---

## Current Status

| Feature | Status | Notes |
|---------|--------|-------|
| BSP Boot | ✅ Complete | Single-core boot working with banner |
| AP Boot | ✅ Complete | Multi-core boot with state management |
| Interrupt Handling | ✅ Complete | IDT and exception handlers working |
| APIC Init | ✅ Complete | Local APIC and I/O APIC |
| APIC Timer | ✅ Complete | High-frequency timer interrupts |
| Device Framework | ✅ Complete | probe/init/remove pattern |
| Physical Memory Manager | ✅ Complete | Buddy system allocator with Multiboot2 |
| Synchronization Primitives | ✅ Complete | Spin locks, RW locks, IRQ-safe locks |
| Test Framework | ✅ Complete | PMM and spin lock test suites |
| ACPI Parsing | ⚠️ Partial | Using default APIC IDs for now |
| RTC Timer | ❌ Removed | Removed due to QEMU reset issues |

---

## Technical Highlights

### AP Trampoline Design
AP startup trampoline uses Position-Independent Code (PIC) with GOT-based symbol resolution:
- **16-bit** → **32-bit** using `data32 ljmp` far jump
- **32-bit** → **64-bit** using `retf` technique
- No runtime patching needed; linker populates GOT

### Buddy System Memory Allocator
Based on the Linux kernel buddy algorithm:
- **Order-based allocation** - Supports 2^0 to 2^9 pages (4KB - 2MB)
- **Automatic coalescing** - Merges adjacent free blocks on free
- **Split allocation** - Automatically splits larger blocks when needed
- **O(1) operations** - Free list per order

### Spin Lock System
Complete SMP synchronization primitives:
- **Basic locks** - TAS implementation using `xchg` instruction
- **IRQ-safe locks** - Saves/restores RFLAGS to prevent interrupt deadlocks
- **Read-write locks** - Atomic counter implementation, multiple readers
- **PAUSE instruction** - Reduces power consumption while spinning

### Assembly Mnemonic Optimization
```assembly
/* Before: Hardcoded bytes */
.byte 0x66
.byte 0xEA
.long 0x703D
.word 0x0008

/* After: Readable mnemonic */
data32 ljmp $PM_CODE_SELECTOR, $pm_entry
```

### Device Driver Framework
Linux-style three-phase initialization:
1. **Register Drivers** - `driver_register()`
2. **Probe Devices** - `device_probe()` (match by match_id/match_mask)
3. **Init Devices** - Execute in `init_priority` order

---

## About LLM Generation

All code in this project is generated via **Claude Code** (claude.ai/code):

- **Zero Hand-Written Code** - All C/assembly code generated through conversation
- **Natural Language Driven** - Describe requirements in Chinese, LLM generates code
- **Iterative Development** - Gradually improve functionality through debug output
- **Exploratory Learning** - Understanding OS principles with LLM assistance

---

## Debug Output Example

```
+============================================================+
|  Emergence Kernel - Intelligence through Emergence      |
+============================================================+

PMM: Initialized
[ PMM tests ] Running allocation tests...
[ PMM tests ] Allocated page1 at 0x1000000, page2 at 0x1001000
[ PMM tests ] Allocated 32KB block at 0x1002000
[ PMM tests ] Freed pages (buddy coalescing)
[ PMM tests ] Free: 0x1F8 / Total: 0x200
[ PMM tests ] Allocated 2-page block at 0x1000000 (should be same as page1 if coalesced)
[ PMM tests ] Tests complete
BSP: Initializing...
SMP: Starting spin lock tests...
[ Spin lock tests ] Starting spin lock test suite...
[ Spin lock tests ] Number of CPUs: 2
[ Spin lock tests ] === Single-CPU Tests ===
[ Spin lock tests ] Test 1: Basic lock operations...
[ Spin lock tests ] Test 1 PASSED
...
[ Spin lock tests ] Result: ALL TESTS PASSED
SMP: All spin lock tests PASSED
System: Shutdown complete
```

---

## License

MIT License - see [LICENSE](LICENSE) for details.

---

## References

- [Intel SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html) - x86_64 Architecture Manual
- [OSDev Wiki](https://wiki.osdev.org/) - OS Development Documentation
- [Multiboot2 Specification](https://www.gnu.org/software/grub/manual/multiboot/multiboot.html) - GRUB Boot Protocol
