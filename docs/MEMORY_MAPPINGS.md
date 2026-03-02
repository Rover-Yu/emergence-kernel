# Kernel Memory Mappings Layout Design

## Overview

This document describes the virtual and physical memory layout of the Emergence Kernel, including boot page tables, nested kernel mappings, and all reserved memory regions.

---

## 1. Physical Memory Layout

### 1.1 Low Memory (0 - 1MB)

| Address Range | Size | Component | Description |
|---------------|------|------------|-------------|
| 0x0000 - 0x6FFF | ~28 KB | BIOS data area | Reserved by BIOS |
| 0x7000 - 0x7FFF | 4 KB | AP Trampoline Code | Real/Protected mode entry |
| 0x7E00 - 0x7DFF | 8 B | GDT32 | Protected mode GDT |
| 0x7F00 - 0x7F17 | 24 B | GDT64 | Long mode GDT |
| 0x8000 - 0x8FFF | 4 KB | Trampoline Stub | 64-bit entry stub |
| 0x9000 - 0x91FF | 32 B | GOT | Global Offset Table |
| 0xA000 - 0xFFFFF | ~22 KB | Reserved | Gap region |

### 1.2 Conventional Memory (1MB - 4MB)

| Address Range | Size | Component | Description |
|---------------|------|------------|-------------|
| 0x100000 - 0x1FFFFF | 1 MB | **Current Kernel Location** | Kernel ELF load address |
| 0x200000 - 0x2FFFFF | 1 MB | User Stack Region | PMM allocation area |
| 0x300000 - 0x3FFFFF | 1 MB | Available | Currently unused |

**Note:** This 1MB-4MB range is targeted for GRUB2 use after kernel relocation.

### 1.3 High Memory Regions

| Address Range | Size | Component | Description |
|---------------|------|------------|-------------|
| 0x400000+ | Variable | **New Kernel Location** | After relocation |
| 0xFEE00000 | 4 KB | Local APIC | MMIO region, uncached |

---

## 2. Boot Page Table Structure

### 2.1 4-Level Paging Architecture

```
x86-64 4-Level Paging:
┌─────────────────────────────────────────────────────────────────┐
│ PML4 (Page Map Level 4)                                │
│   - Maps 256 TB of virtual address space               │
│   - 512 entries × 8 bytes = 4 KB                    │
├─────────────────────────────────────────────────────────────────┤
│ PDPT (Page Directory Pointer Table)                         │
│   - Maps 512 GB                                       │
│   - 512 entries × 8 bytes = 4 KB                    │
├─────────────────────────────────────────────────────────────────┤
│ PD (Page Directory)                                       │
│   - Maps 1 GB with 2 MB pages                         │
│   - 512 entries × 8 bytes = 4 KB                    │
│   - PS bit set for 2 MB pages                          │
├─────────────────────────────────────────────────────────────────┤
│ PT (Page Table) - NOT USED in boot                       │
│   - Would map 2 MB with 4 KB pages                    │
└─────────────────────────────────────────────────────────────────┘
```

### 2.2 Boot Page Table Layout

**File:** `arch/x86_64/boot.S`

#### PML4 (boot_pml4)
| Entry | Index | Value | Description |
|--------|---------|-------|-------------|
| 0 | 0 | Points to PDPT (identity map) |
| 3 | 3×8 | Points to PD (for APIC high mapping) |
| 0x1FD | 0x1FD×8 | Points to PDPT (for 0xFEE00000 APIC) |

#### PDPT (boot_pdpt)
| Entry | Index | Value | Description |
|--------|---------|-------|-------------|
| 0 | 0 | Points to PD (identity map) |
| 3 | 3×8 | Points to PD (for APIC high mapping) |

#### PD (boot_pd) - Identity Map First 1GB
| Entry | Range | Type | Description |
|--------|---------|-------|-------------|
| [0-511] | 0-512×2MB | 2MB Pages | Identity map 0-1GB |
| 503 | Special | APIC MMIO | Maps 0xFEE00000, uncached |

**Identity Mapping Details:**
- VA 0x00000000 - 0x3FFFFFFF → PA 0x00000000 - 0x3FFFFFFF (1GB)
- Uses 2 MB pages for efficiency (PS=1)
- Covers: kernel code, data, stacks, all low memory structures

### 2.3 APIC High Mapping

**Purpose:** Access Local APIC at fixed MMIO address 0xFEE00000

**Virtual Address Calculation:**
```
VA 0xFEE00000 = 0b11111110111000000000000000000000000
PML4 index = bits 39-47 = 0x1FD
PDPT index = bits 30-38 = 0x3
PD index   = bits 21-29 = 0x1F8 (504)
```

**Implementation:** `arch/x86_64/boot.S:220-255, 273-285`
- Set PML4[0x1FD] → PDPT[3] → PD
- PD[503] maps 0xFEE00000 with uncached attribute (0x9B)
- Entry flags: Present + Writable + Write-Through + Cache-Disable + PS (2MB)

---

## 3. Nested Kernel Page Tables

### 3.1 Purpose

The nested kernel architecture uses a **CR0.WP toggle design** for privilege separation:
- **OK Mode (Outer Kernel):** CR0.WP=1, write protection enforced for read-only pages
- **NK Mode (Nested Kernel):** CR0.WP=0, can write to read-only pages (privileged)

### 3.2 Design: Single Shared Page Table with CR0.WP Toggle

**Key Insight:** Instead of dual page tables with CR3 switching, we use a single shared page table with CR0.WP bit toggling:

| Mode | CR0.WP | Page Table | Write to Read-Only |
|------|--------|------------|-------------------|
| OK (unprivileged) | 1 | unpriv_pml4 | **Blocked by hardware** |
| NK (privileged) | 0 | unpriv_pml4 | **Allowed by hardware** |

**Benefits:**
- Eliminates TLB flushes on every monitor call (no CR3 switch)
- Simpler implementation with single page table
- Better performance for NK/OK transitions

**Limitation:**
- Privileged register integrity (CR0, CR3, CR4, etc.) is NOT enforced
- Protection focuses on page table integrity only

### 3.3 Shared Page Table Structure

**File:** `kernel/monitor/monitor.c`

| Symbol | Purpose | Location |
|---------|---------|----------|
| unpriv_pml4 | Shared PML4 (NK & OK) | Allocated via PMM |
| unpriv_pdpt | Shared PDPT | Allocated via PMM |
| unpriv_pd | Shared PD | Allocated via PMM |
| unpriv_pt_0_2mb | 4KB PT for 0-2MB | Fine-grained protection |

**Monitor Page Tables (allocated but not used for CR3 switching):**

| Symbol | Purpose | Location |
|---------|---------|----------|
| monitor_pml4 | Monitor PML4 (reference) | Allocated via PMM |
| monitor_pdpt | Monitor PDPT (reference) | Allocated via PMM |
| monitor_pd | Monitor PD (reference) | Allocated via PMM |
| monitor_pt_0_2mb | 4KB PT for 0-2MB | Writable reference |

**Note:** Monitor page tables are kept as writable reference copies for NK operations. The trampoline toggles CR0.WP instead of switching CR3.

### 3.4 Protection Mechanism

**PTP (Page Table Page) Protection:**
- PTPs are marked **read-only** in the shared page table
- In OK mode (CR0.WP=1): Hardware blocks writes to read-only pages
- In NK mode (CR0.WP=0): Hardware allows writes to read-only pages

**4KB Page Table for First 2MB Region:**
```
unpriv_pt_0_2mb[0-511]:
  - Boot page tables (boot_pml4, boot_pdpt, etc.): Read-only
  - Monitor page tables: Read-only
  - Other pages: Writable
```

**Shared Page Table Characteristics:**
- Page table pages (PTPs): **Read-only, supervisor-only** (protection)
- Kernel code/data: **Supervisor-only, RW** (normal operation)
- User stack region: **User-accessible, writable** (for user mode)
- APIC MMIO: **User-accessible** (for I/O in user mode)

---

## 4. Kernel Virtual Address Map

### 4.1 Current Layout (Kernel at 4MB)

**Status:** Kernel relocated to 4MB to avoid GRUB2 reserved memory gap (1MB-4MB).

```
Virtual Address Space:
┌────────────────────────────────────────────────────────────────┐
│ Identity Mapped Region (0 - 1GB)                        │
│   VA = PA for entire 1GB region                          │
│                                                             │
│   Contains:                                                 │
│   - AP Trampoline (0x7000 - 0x9FFF)                      │
│   - GRUB2 Reserved Gap (0x100000 - 0x3FFFFF)            │
│   - Kernel Code/Data (0x400000+)                            │
│   - Stacks (at various addresses)                             │
│   - Page Tables                                             │
│   - APIC MMIO (high mapping)                               │
├────────────────────────────────────────────────────────────────┤
│ High Memory (Non-identity mapped)                           │
│   Requires special PML4 entries for access                    │
└────────────────────────────────────────────────────────────────┘
```

**Rationale for 4MB Location:**
- GRUB2 requires 1MB-4MB region for its own use during boot
- Loading kernel above 4MB prevents PMM from allocating in GRUB2's reserved area
- Multiboot2 info structure is still properly reserved and parsed
- AP trampoline remains at fixed 0x7000 location (unchanged)

### 4.2 Special Symbol Locations (BSS Symbols)

| Symbol | Address | Size | Section |
|---------|----------|-------|----------|
| _kernel_start | 0x100000 | Variable | Linker-defined |
| _kernel_end | ~0x140000 | Variable | Linker-defined |
| nk_boot_stack_bottom | 0x112010 | 16 KB | BSP boot stack |
| nk_boot_stack_top | 0x116010 | End of BSP stack | |
| nk_trampoline_stack_bottom | 0x119000 | 16 KB | AP temp stack |
| nk_trampoline_stack_end | 0x11d000 | End of AP stack | |
| ok_cpu_stacks[0] | 0x126020 | 16 KB | CPU 0 stack |
| ok_cpu_stacks[1] | 0x12a020 | 16 KB | CPU 1 stack |
| ok_cpu_stacks[2] | 0x12e020 | 16 KB | CPU 2 stack |
| ok_cpu_stacks[3] | 0x132020 | 16 KB | CPU 3 stack |
| kernel_cmdline | 0x1361a0 | 1 KB | Cmdline buffer |
| embedded_cmdline | 0x110c50 | ~12 B | Fallback string |

---

## 5. Memory Region Reservation

### 5.1 PMM Reserved Regions

**File:** `kernel/pmm.c`

| Region | Physical Address | Size | Purpose |
|---------|------------------|-------|---------|
| Multiboot Info | Runtime | Page-aligned | Bootloader data |
| Kernel | _kernel_start | kernel_end - kernel_start | Kernel code/data/BSS |
| AP Trampoline | 0x7000 | 12 KB | Fixed location trampoline |
| Boot Stacks | kernel_end | 32 KB | BSP + trampoline stacks |

### 5.2 Reservation Order

```c
// pmm.c: pmm_init() sequence
1. Parse multiboot info → Reserve MBI region
2. Add available memory from multiboot map
3. Reserve kernel region (using linker symbols)
4. Reserve AP trampoline (fixed at 0x7000)
5. Reserve boot stacks (after kernel_end)
```

---

## 6. Current Layout After Kernel Relocation

**Status:** ✅ **COMPLETE** - Kernel successfully relocated to 4MB (as of commit `00e55a1`)

### 6.1 Physical Memory (Kernel at 4MB)

| Address Range | Size | Component | Description |
|---------------|------|------------|-------------|
| 0x7000 - 0x9FFF | 12 KB | AP Trampoline | **UNCHANGED** |
| 0x100000 - 0x3FFFFF | 3 MB | **GRUB2 Gap** | Reserved for bootloader |
| 0x400000 - 0x4FFFFF | 1 MB | **New Kernel Location** | Main kernel code/data |
| 0xFEE00000 | 4 KB | Local APIC | **UNCHANGED** |

### 6.2 Page Table Placement

**Page Tables Have Moved with Kernel:**

Page tables (boot_pml4, boot_pdpt, boot_pd, etc.) are defined in `.bss` section:

```
Linker Script Section Order:
.text → .rodata → .data → .bss

When . = 1M:  .bss ends at ~0x140000 (within 1-4MB)
When . = 4M:  .bss ends at ~0x440000 (above 4MB) ✅ CURRENT
```

**Page Table Symbols in .bss (at 4MB kernel base):**
| Symbol | Size | Section | Address (est.) |
|---------|-------|----------|---------------|
| boot_pml4 | 4 KB | .bss | ~0x440000 |
| boot_pdpt | 4 KB | .bss | ~0x441000 |
| boot_pd | 4 KB | .bss | ~0x442000 |
| boot_pd_apic | 4 KB | .bss | ~0x443000 |
| boot_pt_apic | 4 KB | .bss | ~0x444000 |

✅ **All page tables now located above 4MB automatically** (when kernel base changed to 4MB).

### 6.3 GRUB2 Gap Reservation

**Status:** ✅ **IMPLEMENTED** - PMM reserves 1MB-4MB gap at init

The PMM now reserves the GRUB2 gap during initialization:

```c
// kernel/pmm.c:278-279
serial_puts("PMM: Reserving GRUB2 gap at 0x100000, size 3145728 bytes\n");
pmm_reserve_region(0x100000, 0x300000);  // 3MB gap (1MB-4MB)
```

**Current PMM Reservation Order (from `pmm_init()`):**

1. **Multiboot Info** (runtime address from MBI) - Reserved after parsing
2. **Kernel** (0x400000 - kernel_end) - Reserved using linker symbols
3. **AP Trampoline** (0x7000 - 0x9FFF, 12KB) - Reserved at fixed location
4. **Boot Stacks** (nk_boot_stack_bottom, 16KB) - Reserved after kernel
5. **GRUB2 Gap** (0x100000 - 0x3FFFFF, 3MB) - Reserved for bootloader

**Note:** This order matches the `pmm_init()` sequence in `kernel/pmm.c:213-279`.

---

## 7. Virtual Memory Protection Zones

### 7.1 Nested Kernel Protection (CR0.WP Toggle Design)

**File:** `kernel/monitor/monitor.c`

| Region | OK Mode (CR0.WP=1) | NK Mode (CR0.WP=0) | Purpose |
|---------|-------------------|-------------------|---------|
| Page Tables (PTPs) | Read-only, Supervisor | Writable | Protect page table integrity |
| Kernel Code | Supervisor, RO | Writable | Allow syscall execution |
| Stacks | RW | RW | Allow normal operation |
| Critical Data | Read-only | Writable | Nested kernel protection |

**Protection Mechanism:**
1. PTPs are marked read-only in the shared page table PTEs
2. Hardware enforces write protection when CR0.WP=1 (OK mode)
3. Trampoline clears CR0.WP for NK mode operations
4. Trampoline restores CR0.WP when returning to OK mode

### 7.2 Monitor Trampoline (CR0.WP Toggle)

**File:** `arch/x86_64/monitor/monitor_call.S`

```
NK Entry (monitor_call):
  1. Save CR0 to per-CPU data (gs:40)
  2. Clear CR0.WP (bit 16) → Enter NK mode
  3. Call monitor_call_handler()
  4. Restore CR0 from per-CPU data → Return to OK mode
  5. Return to caller
```

**Per-CPU Data Structure:**
```c
// arch/x86_64/smp.h
typedef struct {
    uint64_t saved_rsp;     /* Offset 0 */
    uint64_t saved_cr3;     /* Offset 8 - for future use */
    int cpu_index;          /* Offset 16 */
    uint64_t saved_rax;     /* Offset 24 */
    uint64_t saved_rdx;     /* Offset 32 */
    uint64_t saved_cr0;     /* Offset 40 - CR0.WP state */
} per_cpu_data_t;
```

### 7.3 User Stack Region

**File:** `kernel/monitor/monitor.c`

```c
// Make 0x200000-0x3FFFFF user-accessible
unpriv_pd[1] |= X86_PTE_USER;
```

**Purpose:** PMM allocates from this region for user-mode stacks.

---

## 8. IDT and Interrupt Handling

### 8.1 IDT Structure

**File:** `arch/x86_64/idt.c`

| Component | Location | Size | Description |
|-----------|----------|-------|-------------|
| idt[256] | BSS | 2 KB | Interrupt Descriptor Table |
| idt_ptr | BSS | 10 B | IDT pointer for lidt |

### 8.2 IDT Entry Format

```c
typedef struct {
    uint16_t offset_low;   // Handler address [15:0]
    uint16_t selector;     // Code segment selector
    uint8_t  ist;          // Interrupt Stack Table (0 = none)
    uint8_t  type_attr;    // Gate type + DPL + P
    uint16_t offset_mid;   // Handler address [31:16]
    uint32_t offset_high;  // Handler address [63:32]
    uint32_t zero;         // Reserved (must be 0)
} __attribute__((packed)) idt_entry_t;
```

---

## 9. Memory Type Attributes

### 9.1 Page Table Entry Flags

**File:** `arch/x86_64/boot.S`, `kernel/monitor/monitor.c`

| Bit | Name | Value | Purpose |
|------|---------|-------|---------|
| 0 | Present | 0x001 | Page is present |
| 1 | Writable | 0x002 | Page is writable |
| 2 | User | 0x004 | User-accessible (U/S=0) |
| 3 | Write-Through | 0x008 | Write-through caching |
| 4 | Cache-Disable | 0x010 | Disable caching (UC) |
| 5 | Accessed | 0x020 | Software managed |
| 6 | Dirty | 0x040 | Software managed |
| 7 | Page Size | 0x080 | 0=4KB, 1=2MB/4MB |
| 8 | Global | 0x100 | Global page (ignored in PAE) |

### 9.2 Typical Entry Values

```c
// Normal 2MB page (identity mapping)
0x187 = Present + Writable + User + PS (2MB)

// APIC MMIO (uncached)
0x9B  = Present + Writable + WT + CD + PS (strict UC)

// User-accessible 4KB page
0x007 = Present + Writable + User

// Read-only page table page
0x001 = Present only
```

---

## 10. References

### 10.1 Key Source Files

| File | Purpose |
|-------|---------|
| `arch/x86_64/linker.ld` | Kernel load address definition |
| `arch/x86_64/boot.S` | Boot page table setup, stack definitions |
| `arch/x86_64/multiboot2.c` | MBI parsing, cmdline buffer |
| `kernel/pmm.c` | Physical memory manager, reservations |
| `kernel/monitor/monitor.c` | Nested kernel page tables |
| `arch/x86_64/smp.c` | AP startup, per-CPU stacks |
| `arch/x86_64/idt.c` | Interrupt Descriptor Table |
| `arch/x86_64/apic.c` | Local APIC driver |

### 10.2 Related Documentation

- `docs/monitor_trampoline.md` - Nested kernel trampoline details
- `docs/monitor_api.md` - Monitor API documentation
- `CLAUDE.md` - Build system overview (project root)

---

## Changelog

| Version | Date | Changes |
|---------|--------|---------|
| 1.2 | 2026-03-03 | Updated nested kernel section for CR0.WP toggle design (single shared page table) |
| 1.1 | 2025-02-12 | Updated to reflect completed kernel relocation to 4MB (was "proposed", now implemented) |
| 1.0 | 2025-01-XX | Initial documentation |

---

*Document Version: 1.2*
*Last Updated: 2026-03-03*
