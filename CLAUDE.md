# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**JAKernel** is an educational x86-64 operating system kernel demonstrating OS fundamentals including multi-processor support (SMP), device driver frameworks, and interrupt handling. The kernel boots via GRUB2 multiboot2 and runs on QEMU.

## Build and Run Commands

```bash
# Build the kernel ISO
make

# Run in QEMU with 4 CPUs
make run

# Run in QEMU with GDB debugging (listens on localhost:1234)
make run-debug

# Clean build artifacts
make clean
```

## Architecture Overview

### Boot Process

The kernel has a sophisticated multi-processor boot sequence:

1. **BSP (Bootstrap Processor) Path**: Starts at `_start` in `arch/x86_64/boot.S`
   - 32-bit protected mode entry via multiboot2
   - Enables PAE, sets up 4-level page tables (PML4 → PDPT → PD)
   - Transitions to x86-64 long mode
   - Calls `kernel_main()`

2. **AP (Application Processor) Path**: Woken via STARTUP IPI
   - APs begin execution at physical address 0x7000 (`.ap_trampoline` section)
   - Real mode → protected mode → long mode transition
   - Call `ap_start()` in `kernel/smp.c`

3. **BSP/AP Detection**: Uses atomic increment of `cpu_boot_counter` at boot
   - First CPU (counter = 1) is BSP
   - Subsequent CPUs are APs and wait for `bsp_init_done` flag

### Memory Layout (defined in `arch/x86_64/linker.ld`)

- **0x0000 - 0x7000**: Multiboot2 header
- **0x7000 - 0x8000**: AP trampoline code (real mode entry for APs)
- **1MB+**: Kernel proper (text, rodata, data, bss sections)

### Key Subsystems

**Device Driver Framework** (`kernel/device.c`):
- Match/probe/init lifecycle model
- Priority-based initialization ordering
- Driver registry (singly linked list) and device registry
- ID matching using bitmask for device-driver compatibility

**SMP** (`kernel/smp.c`):
- Supports up to 4 CPUs (`SMP_MAX_CPUS`)
- Per-CPU stacks and state tracking
- APIC ID mapping (default 0,1,2,3 when ACPI unavailable)
- Atomic CPU ID assignment using `__sync_fetch_and_add`

**Interrupt System**:
- IDT setup in `arch/x86_64/idt.c`
- Local APIC driver in `arch/x86_64/apic.c` for IPI delivery
- RTC timer driver (`arch/x86_64/rtc.c`) for periodic interrupts
- ISR stubs in `arch/x86_64/isr.S`

### Page Table Mapping

The kernel identity-maps the first 1GB of physical memory using 2MB pages. For APIC access at 0xFEE00000, the page tables use a clever trick: `PDPT[3]` points to the same page directory as `PDPT[0]`, allowing access to high-memory regions through the same PD entries.

## Code Organization

```
arch/x86_64/          # Architecture-specific code
├── boot.S           # Boot sequence, mode switching, AP trampoline
├── isr.S            # Interrupt service routine stubs
├── apic.c           # Local APIC and IPI handling
├── idt.c            # Interrupt Descriptor Table setup
├── ipi.c            # IPI driver (device framework integration)
├── timer.c/rtc.c    # Timer drivers
├── serial_driver.c  # Serial port output (debug)
└── vga.c            # VGA text mode display

kernel/              # Architecture-independent code
├── main.c           # Kernel entry point, initialization orchestration
├── device.c         # Device driver framework
└── smp.c            # Multi-processor coordination
```

## Development Notes

- **Compiler flags**: `-ffreestanding -mcmodel=large -mno-red-zone` (bare metal, large code model, no red zone)
- **Testing**: The `tests/` directory contains reference implementations (not compiled into kernel)
- **Debug output**: Serial port (COM1: 0x3F8) is primary debug output; VGA is secondary
- **SMP timing**: QEMU-specific optimizations use shorter delays (1ms vs 10ms) for IPI delivery
- **ACPI**: Currently disabled; uses default APIC IDs (0-3)

## Common Patterns

**Device Registration**:
```c
struct driver my_driver = {
    .name = "my_driver",
    .match_id = 0x1234,
    .match_mask = 0xFFFF,
    .probe = my_probe,
    .init = my_init,
};
driver_register(&my_driver);
```

**Per-CPU Operations**:
- Use `smp_get_cpu_index()` to get current CPU index (0-3)
- Use `smp_get_apic_id()` to get current CPU's APIC ID
- BSP is always CPU 0; check with `if (cpu_id == 0)`
