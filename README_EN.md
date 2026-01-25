# JAKernel

> **JAKernel** is an experimental x86_64 operating system kernel written entirely by LLM (Claude) through prompts - **zero hand-written code**.

This is an exploratory educational project aimed at demonstrating the potential of Large Language Models in OS kernel development. All code is generated through natural language conversations, including low-level assembly, memory management, and multiprocessor support.

---

## Features

### Currently Implemented

#### Boot and Mode Transitions
- **Multiboot2 Boot** - Kernel loaded via GRUB
- **Real Mode → Protected Mode → Long Mode** - Complete mode transition flow
- **Page Table Setup** - Identity mapping for first 2MB
- **GDT Configuration** - 64-bit Global Descriptor Table

#### Symmetric Multi-Processing (SMP)
- **BSP/AP Detection** - Distinguish Bootstrap vs Application Processors via atomic counter
- **AP Startup Trampoline** - Jump from real mode to long mode
- **IPI Communication** - Inter-Processor Interrupt support
- **Per-CPU Stack** - Independent stack space (16 KiB each)

#### Interrupt and Exception Handling
- **IDT Setup** - Complete Interrupt Descriptor Table
- **Exception Handlers** - Divide by zero, page fault, etc.
- **ISR Stubs** - Assembly interrupt service routines

#### Device Driver Framework
- **Linux-style Driver Model** - probe/init/remove pattern
- **Device Types** - PLATFORM, ISA, PCI, SERIAL, CONSOLE
- **Priority-based Init** - Support for driver initialization ordering
- **Matching Mechanism** - match_id/match_mask device matching

#### APIC (Advanced Programmable Interrupt Controller)
- **Local APIC** - Per-CPU interrupt controller initialization
- **I/O APIC** - External interrupt routing
- **IPI Support** - Inter-processor interrupt send/receive

#### Console Output
- **VGA Text Mode** - 80x25 character display
- **Serial Output** (COM1) - Debug information output

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
JAKernel/
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
│   ├── rtc.c            # RTC driver
│   └── ipi.c            # Inter-Processor Interrupts
├── kernel/              # Architecture-independent code
│   ├── main.c           # Kernel main function
│   ├── smp.c            # Multiprocessor support
│   └── device.c         # Device driver framework
├── tests/               # Test code
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
| BSP Boot | ✅ Complete | Single-core boot working |
| AP Boot | ✅ Complete | 3/3 APs successfully start |
| Interrupt Handling | ✅ Complete | IDT and exception handlers working |
| APIC Init | ✅ Complete | Local APIC and I/O APIC |
| Device Framework | ✅ Complete | probe/init/remove pattern |
| Timers | ⚠️ Partial | RTC disabled due to reset issues |
| ACPI Parsing | ⚠️ Partial | Using default APIC IDs for now |

---

## Technical Highlights

### AP Trampoline Design
AP startup trampoline uses Position-Independent Code (PIC) with GOT-based symbol resolution:
- **16-bit** → **32-bit** using `data32 ljmp` far jump
- **32-bit** → **64-bit** using `retf` technique
- No runtime patching needed; linker populates GOT

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
Hello, JAKernel!
BSP: Initializing...
[APIC] Local APIC initialized
BSP: Initialization complete
CPU 0 (APIC ID 0): Successfully booted
SMP: Starting APs...
SMP: Starting AP 1...
HG3APLXDSQWYAT568J12JIMI
[AP] CPU 1 initialized successfully
SMP: Starting AP 2...
HG3APLXDSQWYAT568J12JIMI
[AP] CPU 2 initialized successfully
SMP: Starting AP 3...
HG3APLXDSQWYAT568J12JIMI
[AP] CPU 3 initialized successfully
SMP: All APs startup complete. 3/3 APs ready
```

---

## License

MIT License - see [LICENSE](LICENSE) for details.

---

## References

- [Intel SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html) - x86_64 Architecture Manual
- [OSDev Wiki](https://wiki.osdev.org/) - OS Development Documentation
- [Multiboot2 Specification](https://www.gnu.org/software/grub/manual/multiboot/multiboot.html) - GRUB Boot Protocol
