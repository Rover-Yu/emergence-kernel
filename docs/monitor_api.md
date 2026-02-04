# Nested Kernel Monitor API

This document describes the Nested Kernel Monitor API for the Emergence Kernel, implementing the architecture from "Nested Kernel: An Operating System Architecture for Intra-Kernel Privilege Separation" (ASPLOS '15).

## Overview

The Nested Kernel architecture splits the kernel into two privilege domains:
- **Monitor (Nested Kernel)**: Privileged mode, full access to all memory and page tables
- **Outer Kernel**: Unprivileged mode, restricted access following Nested Kernel invariants

## Core Concepts

### Page Table Views

The monitor maintains two separate page table views:

| View | CR3 Value | Purpose |
|------|-----------|---------|
| **Privileged** | `monitor_pml4_phys` | Full system access, PTPs writable |
| **Unprivileged** | `unpriv_pml4_phys` | Restricted access, PTPs read-only |

### Nested Kernel Invariants

The monitor enforces 6 invariants from the ASPLOS '15 paper:

| Invariant | Description |
|-----------|-------------|
| **Inv 1** | Protected data (PTPs) are read-only in outer kernel |
| **Inv 2** | Write-protection permissions enforced (CR0.WP=1) |
| **Inv 3** | Global mappings accessible in both views |
| **Inv 4** | Context switch mechanism available |
| **Inv 5** | All PTPs writable in nested kernel |
| **Inv 6** | CR3 only loads pre-declared PTPs |

## API Reference

### Initialization

#### `monitor_init(void)`

Initialize the nested kernel monitor.

**Call from:** BSP only, during kernel initialization

**Side effects:**
- Allocates 6 page table pages (3 for monitor, 3 for unprivileged view)
- Copies boot page table mappings to both views
- Protects PTPs in unprivileged view (Invariant 1 & 5)
- Sets `monitor_pml4_phys` and `unpriv_pml4_phys`

**Example:**
```c
extern void monitor_init(void);

void kernel_main(void) {
    // ...
    monitor_init();
    // ...
}
```

---

### Verification

#### `monitor_verify_invariants(void)`

Verify all 6 Nested Kernel invariants are correctly enforced.

**Call from:** After switching to unprivileged mode (both BSP and APs)

**Output behavior:**
- **Quiet mode** (`CONFIG_INVARIANTS_VERBOSE=0`): Shows only final result
  ```
  [CPU 0] Nested Kernel invariants: PASS
  ```
- **Verbose mode** (`CONFIG_INVARIANTS_VERBOSE=1`): Shows per-invariant details

**Example:**
```c
extern void monitor_verify_invariants(void);

// After switching to unprivileged mode
uint64_t unpriv_cr3 = monitor_get_unpriv_cr3();
asm volatile ("mov %0, %%cr3" : : "r"(unpriv_cr3) : "memory");
monitor_verify_invariants();
```

---

### Page Table Switch

#### `monitor_get_unpriv_cr3(void)`

Get the physical address of the unprivileged page table root.

**Returns:** Physical address of unprivileged PML4

**Example:**
```c
extern uint64_t monitor_get_unpriv_cr3(void);

uint64_t unpriv_cr3 = monitor_get_unpriv_cr3();
if (unpriv_cr3 != 0) {
    asm volatile ("mov %0, %%cr3" : : "r"(unpriv_cr3) : "memory");
}
```

---

### Privilege Mode Detection

#### `monitor_is_privileged(void)`

Check if currently running in privileged (monitor) mode.

**Returns:** `true` if privileged, `false` if unprivileged

**Example:**
```c
extern bool monitor_is_privileged(void);

if (monitor_is_privileged()) {
    // Can modify PTPs directly
} else {
    // Must use monitor_call() for privileged operations
}
```

---

### Monitor Calls

#### `monitor_call(monitor_call_t, arg1, arg2, arg3)`

Invoke a privileged monitor operation from unprivileged mode.

**Parameters:**
- `call`: Monitor call type
- `arg1`, `arg2`, `arg3`: Call-specific arguments

**Returns:** `monitor_ret_t` structure with `result` and `error` fields

**Monitor Call Types:**

| Call | Description | arg1 | arg2 | arg3 |
|------|-------------|------|------|------|
| `MONITOR_CALL_ALLOC_PHYS` | Allocate physical memory | order (0-9) | - | - |
| `MONITOR_CALL_FREE_PHYS` | Free physical memory | physical address | order | - |

**Example:**
```c
extern monitor_ret_t monitor_call(monitor_call_t, uint64_t, uint64_t, uint64_t);

// Allocate 2 pages (order 1)
monitor_ret_t ret = monitor_call(MONITOR_CALL_ALLOC_PHYS, 1, 0, 0);
if (ret.error != 0) {
    // Handle error
}
void *ptr = (void *)ret.result;
```

---

## Monitor Call Internals

### The Trampoline Mechanism

The `monitor_call()` function uses a trampoline (`nk_entry_trampoline`) to safely switch from unprivileged to privileged mode. This trampoline is a critical security component that ensures controlled privilege transitions.

**How it works:**

1. **Entry (Unprivileged Mode):**
   - Outer kernel calls `monitor_call(call, arg1, arg2, arg3)`
   - Arguments are placed in registers (rdi, rsi, rdx, rcx)
   - Function determines it's in unprivileged mode

2. **Trampoline Execution:**
   - Assembly stub `nk_entry_trampoline` is invoked
   - All registers are saved
   - Current RSP is saved to `saved_rsp` (identity-mapped location)
   - Current CR3 (unprivileged) is saved to r8
   - CR3 is switched to `monitor_pml4_phys` (privileged)
   - RSP is switched to `nk_boot_stack_top` (monitor-only stack)
   - `monitor_call_handler()` is called with original arguments
   - CR3 is restored to unprivileged value (r8)
   - RSP is restored from `saved_rsp`
   - All registers are restored
   - Function returns to outer kernel

3. **Security Benefits:**
   - Outer kernel never directly manipulates CR3
   - Privileged operations only go through the handler
   - Monitor stack is isolated from unprivileged access
   - Full state restoration prevents privilege leakage

**Memory Layout:**
```
Trampoline code:  Identity-mapped in BOTH page tables
saved_rsp:        Identity-mapped in BOTH page tables
Monitor stack:    Only mapped in monitor page tables
```

**Implementation Files:**
- Assembly: `arch/x86_64/monitor/monitor_call.S`
- C wrapper: `kernel/monitor/monitor.c`
- Documentation: `docs/monitor_trampoline.md`

**Testing:**
Enable with `CONFIG_MONITOR_TRAMPOLINE_TEST=1` to run trampoline tests that verify:
- Correct privilege mode transitions
- State preservation across calls
- Multiple call handling
- Proper return to unprivileged mode

For detailed trampoline architecture, see `docs/monitor_trampoline.md`.

---

### PMM Wrapper Functions

#### `monitor_pmm_alloc(uint8_t order)`

Allocate physical memory through monitor.

**Parameters:** `order` - Allocation order (0 = 4KB, 1 = 8KB, etc.)

**Returns:** Physical address, or NULL on failure

#### `monitor_pmm_free(void *addr, uint8_t order)`

Free physical memory through monitor.

**Parameters:**
- `addr`: Physical address to free
- `order`: Allocation order

**Example:**
```c
extern void *monitor_pmm_alloc(uint8_t order);
extern void monitor_pmm_free(void *addr, uint8_t order);

// Allocate 16KB (order 2)
void *ptr = monitor_pmm_alloc(2);
if (ptr) {
    monitor_pmm_free(ptr, 2);
}
```

---

## Paging Constants

From `arch/x86_64/paging.h`:

### PTE Flag Bits

| Bit | Name | Description |
|-----|------|-------------|
| 0 | `X86_PTE_PRESENT` | Page present bit |
| 1 | `X86_PTE_WRITABLE` | Read/write bit (0 = read-only) |
| 2 | `X86_PTE_USER` | User/supervisor bit |
| 7 | `X86_PTE_PS` | Page size bit (1 = 2MB pages) |

### Page Flags

| Flags | Value | Description |
|-------|-------|-------------|
| `X86_PD_FLAGS_2MB` | `0x183` | Standard 2MB page (present + writable + PS) |
| `X86_PD_FLAGS_2MB_RO` | `0x181` | Read-only 2MB page (present + PS only) |

---

## Configuration Options

| Option | Default | Description |
|--------|---------|-------------|
| `CONFIG_WRITE_PROTECTION_VERIFY` | 1 | Always verify invariants on all CPUs |
| `CONFIG_INVARIANTS_VERBOSE` | 0 | Show detailed verification output |

---

## Usage Patterns

### Switching to Unprivileged Mode

```c
// 1. Initialize monitor (BSP only)
monitor_init();

// 2. Switch to unprivileged page tables
uint64_t unpriv_cr3 = monitor_get_unpriv_cr3();
if (unpriv_cr3 != 0) {
    // Enable CR0.WP for enforcement
    uint64_t cr0;
    asm volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= (1 << 16);  // Set CR0.WP bit
    asm volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");

    // Switch to unprivileged view
    asm volatile ("mov %0, %%cr3" : : "r"(unpriv_cr3) : "memory");

    // Verify invariants
    monitor_verify_invariants();
}
```

### Memory Allocation from Unprivileged Mode

```c
// Must use monitor call for memory operations
void *ptr = monitor_pmm_alloc(2);  // Allocate 16KB
if (!ptr) {
    serial_puts("Allocation failed\n");
}

// Use memory
// ...

// Free when done
monitor_pmm_free(ptr, 2);
```

---

## References

**Paper:** "Nested Kernel: An Operating System Architecture for Intra-Kernel Privilege Separation"
- Authors: Nathan Dautenhahn, Theodoros Kasampalis, Will Dietz, John Criswell, Vikram Adve
- Venue: ASPLOS '15
- Website: [nestedkernel.github.io](http://nestedkernel.github.io/)
