# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

JAKernel (gckernel) is an educational x86-64 operating system kernel implementing Symmetric Multi-Processing (SMP). The kernel boots via Multiboot2 using GRUB and runs on QEMU.

**Current Development Focus:** SMP boot implementation - getting Application Processors (APs) to start successfully via STARTUP IPIs.

**Note:** The QEMU machine type was changed from `q35` to `pc` (commit 205e67b) and CPU count reduced to 2 (commit 1c4cc83) for APIC compatibility and easier SMP debugging.

## Build and Run Commands

```bash
# Build the kernel and ISO
make

# Clean build artifacts
make clean

# Run in QEMU (4 CPUs, 128MB RAM)
make run

# Run with GDB debugging (starts QEMU paused, waits for GDB connection)
make run-debug
# Then in another terminal:
gdb -x .gdbinit
```

**QEMU Flags:** `-M pc -m 128M -nographic -cdrom jakernel.iso -smp 2`

## Architecture and Code Layout

```
arch/x86_64/           # Architecture-specific code
├── boot.S             # Multiboot header, BSP/AP boot path, page table setup
├── isr.S              # Interrupt Service Routine wrappers
├── ap_trampoline.bin.S # AP real mode trampoline (built as 16-bit binary)
├── ap_trampoline.ld   # AP trampoline linker script
├── linker.ld          # Main kernel linker script (kernel loads at 1MB)
├── apic.c/.h          # Local APIC driver and IPI support
├── ipi.c/.h           # IPI (Inter-Processor Interrupt) driver
├── acpi.c/.h          # ACPI MADT parsing for CPU discovery
├── idt.c/.h           # Interrupt Descriptor Table
├── timer.c/.h         # Timer interrupt handler (demo quotes)
├── rtc.c/.h           # RTC (Real Time Clock) driver
├── pit.c/.h           # 8259 Programmable Interval Timer driver
├── serial_driver.c    # Serial port driver (COM1)
└── vga.c/.h           # VGA text mode output

kernel/                # Architecture-independent kernel code
├── main.c             # Kernel entry point (kernel_main)
├── smp.c/.h           # SMP subsystem implementation
└── device.c/.h        # Device/driver framework

tests/                 # Test framework (reference code only)
└── test.h             # Test framework header
```

**Note:** The `tests/` directory contains reference test code but is not built by default (empty `TESTS_C_SRCS` in Makefile).

## Boot Flow

### BSP (Bootstrap Processor) Path
1. `_start` (32-bit) in `boot.S` - atomic increment of `cpu_boot_counter` determines BSP (counter=1)
2. Set up 4-level page tables (PML4 → PDPT → PD) with identity mapping
3. Enable PAE → Long Mode → Paging
4. Jump to `long_mode_start` (64-bit) → call `kernel_main()`
5. `kernel_main()` initializes drivers, SMP, IDT, APIC, then calls `smp_start_all_aps()`

### AP (Application Processor) Path
1. BSP sends STARTUP IPI → AP boots at **physical address 0x7000**
2. `ap_trampoline.bin.S` executes: Real Mode → Protected Mode → Long Mode
3. Trampoline jumps to `ap_start()` in C (`kernel/smp.c`)
4. AP gets CPU index, marks itself ready, halts

**Critical:** The AP trampoline contains **placeholders** that are patched at runtime by `patch_ap_trampoline()`:
- `boot_pml4_placeholder` - PML4 address for page tables
- `ap_start_placeholder` - Address of `ap_start()` function
- GDT placeholders (0xAA, 0xBB, 0xCC) - Stack-based GDT loading for position independence

**Debug output:** Trampoline prints "HA" → "1" → "C" → "R" → "P" sequence to track boot stage progress.

## Memory Map

| Address          | Purpose                              |
|------------------|--------------------------------------|
| 0x7000           | AP Trampoline (STARTUP IPI target)   |
| 0x100000 (1MB)   | Kernel code/data load address        |
| 0xB8000          | VGA text mode buffer                 |
| 0xFEE00000       | Local APIC MMIO region               |

## Page Table Layout

The kernel uses 4-level paging for x86-64 Long Mode:
- `PML4[0]` → `PDPT[0]` → `PD[0-511]` → Identity map first 1GB (2MB pages)
- `PML4[0x1FD]` → `PDPT[3]` → `PD[504]` → Map 0xFEE00000 (APIC region)

**APIC mapping detail:** Virtual address 0xFEE00000 requires PML4[0x1FD] and PDPT[3] both pointing to `boot_pd`, then PD[504] maps the physical APIC MMIO region.

## SMP Subsystem

- **Maximum CPUs:** 2 (`SMP_MAX_CPUS` in `kernel/smp.h`) - reduced from 4 for debugging
- **CPU Stack Size:** 16 KB per CPU (`CPU_STACK_SIZE`)
- **CPU States:** OFFLINE, BOOTING, ONLINE, READY
- **Per-CPU Info:** `smp_cpu_info_t` stores apic_id, cpu_index, state, stack_top

**Key functions:**
- `smp_init()` - Initialize per-CPU info with ACPI APIC IDs
- `smp_start_all_aps()` - Send STARTUP IPIs to all APs via `ap_startup()`
- `ap_start()` - AP entry point in C (called from trampoline)
- `patch_ap_trampoline()` - Patch trampoline with runtime addresses

## Local APIC Driver

- **APIC Base:** 0xFEE00000 (identity mapped in page tables)
- **Key functions:**
  - `lapic_init()` - Enable APIC, verify accessibility
  - `ap_startup()` - Send INIT IPI + STARTUP IPI sequence to wake AP
  - `lapic_send_ipi()` - Send IPI to specific APIC ID
  - `lapic_wait_for_ipi()` - Wait for IPI delivery completion (poll ICR)

## Interrupt Vectors

| Vector | Purpose                  |
|--------|--------------------------|
| 0-31   | Exceptions (x86 standard)|
| 32     | Timer (Local APIC)       |
| 33     | IPI                      |
| 40     | RTC (IRQ 8, PIC-based)   |

**Note:** PIC (8259 Programmable Interrupt Controller) is remapped and all IRQs are masked by default in `idt_init()`. RTC interrupts are explicitly disabled due to causing system resets.

## Debugging

Serial port (COM1, 0x3F8) is heavily used for debug output:
- `serial_puts(const char *str)` - Print string
- `serial_putc(char c)` - Print character

GDB debugging: Use `.gdbinit` which connects to QEMU (`target remote :1234`) and sets breakpoint at `kernel_main`.

**Build artifacts:** Object files are organized in `build/` with prefixes `boot_*`, `arch_*`, `kernel_*` for easy identification.

**Code navigation:** Cscope and ctags files are generated in the root directory for code navigation.

## Important Conventions

- **Files:** `kebab-case` naming
- **Functions:** `snake_case` naming
- **Header guards:** `JAKERNEL_<PATH>_<FILE>_H` format
- **Atomic ops:** GCC builtins (`__sync_fetch_and_add`, `__sync_lock_test_and_set`)
- **Volatile:** Used for memory-mapped I/O (APIC) and shared flags (`bsp_init_done`)
- **Attributes:** `__attribute__((packed))`, `__attribute__((aligned(...)))`

## Reference Code

Linux source code is available at `/opt/workbench/os/linux-upstream` for reference on SMP/APIC implementation.
