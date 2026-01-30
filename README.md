# Emergence Kernel

> "We wrote not a single line of code. We only defined the rules of creation."

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
- **APIC Timer** - High-frequency timer interrupts (mathematician quotes output)

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
# Build kernel and ISO (with default configuration)
make

# Run (2 CPUs)
make run

# Run (4 CPUs)
./run-qemu.sh

# Clean build artifacts
make clean
```

### Configuration Options

The kernel supports several configuration options controlled via `kernel.config` or command-line parameters:

| Option | Default | Description |
|--------|---------|-------------|
| `CONFIG_SPINLOCK_TESTS` | 0 | Enable spin lock test suite |
| `CONFIG_PMM_TESTS` | 0 | Enable physical memory manager tests |
| `CONFIG_APIC_TIMER_TEST` | 1 | Enable APIC timer test (mathematician quotes output) |
| `CONFIG_SMP_AP_DEBUG` | 0 | Enable AP startup debug marks (serial output H→G→3→A→P→L→X→D→S→Q→A→I→L→T→W) |
| `CONFIG_WRITE_PROTECTION_VERIFY` | 1 | Verify Nested Kernel invariants on all CPUs |
| `CONFIG_INVARIANTS_VERBOSE` | 0 | Show detailed per-invariant verification output |
| `CONFIG_CR0_WP_CONTROL` | 1 | Enable CR0.WP two-level protection mechanism |

**Configuration Priority:**
1. Command-line: `make CONFIG_SPINLOCK_TESTS=1`
2. Local override: `.config` file (not committed to git)
3. Default: `kernel.config` file

**Examples:**
```bash
# Enable spin lock tests
make CONFIG_SPINLOCK_TESTS=1

# Enable AP debug marks
make CONFIG_SMP_AP_DEBUG=1

# Create local config file (permanent override)
cp kernel.config .config
nano .config  # Edit configuration
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
| APIC Timer | ✅ Complete | High-frequency timer interrupts with math quotes |
| Device Framework | ✅ Complete | probe/init/remove pattern |
| Physical Memory Manager | ✅ Complete | Buddy system allocator with Multiboot2 |
| Synchronization Primitives | ✅ Complete | Spin locks, RW locks, IRQ-safe locks |
| Test Framework | ✅ Complete | PMM, spin lock, and nested kernel invariants tests |
| ACPI Parsing | ⚠️ Partial | Using default APIC IDs for now |
| RTC Timer | ❌ Removed | Removed due to QEMU reset issues |
| **Nested Kernel** | ✅ Complete | All 6 invariants enforced on BSP and APs |
| **Monitor Mode** | ✅ Complete | Privileged/unprivileged page table separation |
| **CR0.WP Protection** | ✅ Complete | Two-level protection (PTE + CR0.WP) |

---

## Technical Highlights

### AP Trampoline Design
AP startup trampoline uses Position-Independent Code (PIC) with GOT-based symbol resolution:
- **16-bit** → **32-bit** using `data32 ljmp` far jump
- **32-bit** → **64-bit** using `retf` technique
- No runtime patching needed; linker populates GOT
- **Conditional debug marks**: Control H→G→3→A→P→L→X→D→S→Q→A→I→L→T→W output via `CONFIG_SMP_AP_DEBUG`

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
- **Memory barrier fix**: Test 6 fixed memory visibility issue for BSP reading test_counter

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

### Configuration System
Flexible build configuration:
- **kernel.config** - Default configuration (committed to git)
- **.config** - Local override (not committed)
- **Command line** - Temporary override `make CONFIG_XXX=1`
- **Conditional compilation** - All test and debug features independently controllable

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
▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓
░░░                                      ░░░
▓▓▓  [ Emergence Kernel ]  v0.1  ▓▓▓
░░░   > Learning with Every Boot   ░░░
▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓

PMM: Initializing...
BSP: Initializing...
APIC: LAPIC_VER = 0x00050014
APIC: APIC version=D maxlvt=5
APIC: Local APIC initialized
BSP: Initialization complete
CPU 0 (APIC ID 0): Successfully booted
SMP: Starting APs...
SMP: All APs startup complete. 1/1 APs ready
[ APIC tests ] 1. Mathematics is queen of sciences. - Gauss
[ APIC tests ] 2. Pure math is poetry of logic. - Einstein
[ APIC tests ] 3. Math reveals secrets to lovers. - Cantor
[ APIC tests ] 4. Proposing questions exceeds solving. - Cantor
[ APIC tests ] 5. God created natural numbers. - Kronecker
system is shutting down
SHUTDOWN: Port I/O failed, halting...
```

---

## License

MIT License - see [LICENSE](LICENSE) for details.

---

## References

- [Intel SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html) - x86_64 Architecture Manual
- [OSDev Wiki](https://wiki.osdev.org/) - OS Development Documentation
- [Multiboot2 Specification](https://www.gnu.org/software/grub/manual/multiboot/multiboot.html) - GRUB Boot Protocol
