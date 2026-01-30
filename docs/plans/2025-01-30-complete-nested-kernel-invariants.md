# Complete Nested Kernel Invariants Implementation (x86-64)

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Complete implementation of all Nested Kernel invariants from the ASPLOS '15 paper, including CR0.WP bit control for proper two-level protection.

**Architecture:** The Nested Kernel paper requires TWO protection levels:
1. **Level 1 (PTE Permissions)**: Mark PTPs read-only in page tables (DONE)
2. **Level 2 (CR0.WP Control)**: Clear CR0.WP in monitor, set in outer kernel (MISSING)

**Tech Stack:** x86-64 assembly, C, QEMU for testing

---

## Current Status Analysis

### Already Implemented (✓)
- **Invariant 1**: PTPs configured read-only in unprivileged view (PTE writable bit = 0)
- **Invariant 5**: All PTPs marked read-only in unpriv_pd
- **Invariant 6**: CR3 loaded with pre-declared PTPs (monitor_pml4_phys, unpriv_pml4_phys)
- TLB invalidation after PTE modifications
- Verification tests confirming invariants are enforced

### Missing (✗)
- **Invariant 2**: Write-protection ENFORCEMENT via CR0.WP bit
- CR0.WP bit manipulation in monitor_call_stub entry/exit
- CR0.WP=1 when switching to unprivileged mode
- CR0.WP=0 when entering monitor (privileged mode)

### Why CR0.WP Matters

From the paper (Section 4.2, x86-64 Implementation):
> "We use CR0.WP to control whether the processor enforces the read-only setting in PTEs.
> When CR0.WP=0, the processor ignores write-protect in PTEs (even for Ring 0).
> When CR0.WP=1, the processor enforces write-protect."

**Without CR0.WP control:**
- PTEs are marked read-only, but CPU at CPL=0 can still write
- Outer kernel (also CPL=0) could bypass protection by writing directly

**With CR0.WP control:**
- Monitor: CR0.WP=0, can modify write-protected PTEs
- Outer kernel: CR0.WP=1, cannot modify write-protected PTEs
- True isolation achieved via MMU enforcement

---

## Implementation Tasks

### Task 1: Add CR0.WP Helper Functions

**File:** `kernel/monitor/monitor.c` (MODIFY)

Add after existing helper functions:

```c
/* CR0 register bit definitions */
#define CR0_WP_BIT (1 << 16)  /* Write Protect bit */

/* Get current CR0 value */
static uint64_t get_cr0(void) {
    uint64_t cr0;
    asm volatile ("mov %%cr0, %0" : "=r"(cr0));
    return cr0;
}

/* Set CR0 value */
static void set_cr0(uint64_t cr0) {
    asm volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");
}

/* Clear CR0.WP bit (allow monitor to write to read-only pages) */
static void monitor_entry_wp_disable(void) {
    uint64_t cr0 = get_cr0();
    cr0 &= ~CR0_WP_BIT;  /* Clear bit 16 */
    set_cr0(cr0);
}

/* Set CR0.WP bit (enforce write protection for outer kernel) */
static void monitor_exit_wp_enable(void) {
    uint64_t cr0 = get_cr0();
    cr0 |= CR0_WP_BIT;  /* Set bit 16 */
    set_cr0(cr0);
}
```

---

### Task 2: Integrate CR0.WP into monitor_call_stub Entry

**File:** `arch/x86_64/monitor/monitor_call.S` (MODIFY)

Modify the entry section (after line 33):

```asm
monitor_call_stub:
    /* Save all registers */
    push %rbx
    push %rbp
    push %r12
    push %r13
    push %r14
    push %r15

    /* Save current RSP and CR3 */
    mov %rsp, %r8
    mov %cr3, %r9

    /* Switch to privileged page tables */
    mov monitor_pml4_phys(%rip), %r10
    mov %r10, %cr3

    /* === NEW: Clear CR0.WP for monitor entry === */
    /* This allows monitor to modify write-protected pages */
    mov %cr0, %r11
    and $0xFFFEFFFF, %r11   /* Clear bit 16 (WP) */
    mov %r11, %cr0

    /* Call C monitor handler */
    mov %rdi, %rdi       /* call */
    mov %rsi, %rsi       /* arg1 */
    mov %rdx, %rdx       /* arg2 */
    mov %rcx, %rcx       /* arg3 */
    call monitor_call_handler

    /* === NEW: Set CR0.WP for monitor exit === */
    /* This enforces write protection for outer kernel */
    mov %cr0, %r11
    or $0x00010000, %r11    /* Set bit 16 (WP) */
    mov %r11, %cr0

    /* Restore unprivileged CR3 */
    mov %r9, %cr3

    /* Restore RSP */
    mov %r8, %rsp

    /* Restore registers */
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbp
    pop %rbx

    ret
```

---

### Task 3: Set CR0.WP=1 When Switching to Unprivileged Mode

**File:** `kernel/main.c` (MODIFY)

Find the CR3 switch section and add CR0.WP control:

```c
        /* Switch to unprivileged page tables */
        uint64_t unpriv_cr3 = monitor_get_unpriv_cr3();
        if (unpriv_cr3 != 0) {
            serial_puts("KERNEL: Switching to unprivileged mode\n");

            /* === NEW: Enable write protection enforcement === */
            /* Set CR0.WP=1 so outer kernel cannot modify read-only PTEs */
            uint64_t cr0;
            asm volatile ("mov %%cr0, %0" : "=r"(cr0));
            cr0 |= (1 << 16);  /* Set CR0.WP bit */
            asm volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");
            serial_puts("KERNEL: CR0.WP enabled (write protection enforced)\n");

            /* Switch to unprivileged page tables */
            asm volatile ("mov %0, %%cr3" : : "r"(unpriv_cr3) : "memory");
            serial_puts("KERNEL: Page table switch complete\n");
        } else {
            serial_puts("KERNEL: Monitor initialization failed\n");
        }
```

---

### Task 4: Add CR0.WP Verification

**File:** `kernel/monitor/monitor.c` (MODIFY)

Add to `monitor_verify_invariants()` function:

```c
/* Verify Nested Kernel invariants are correctly configured */
static void monitor_verify_invariants(void) {
    serial_puts("\n=== Nested Kernel Invariant Verification ===\n");

    /* Get physical address of unpriv_pd to find PD entry */
    uint64_t unpriv_pd_phys = virt_to_phys(unpriv_pd);
    int pd_index = monitor_find_pd_entry(unpriv_pd_phys);

    /* Check the PD entry in both views */
    uint64_t unpriv_entry = unpriv_pd[pd_index];
    uint64_t monitor_entry = monitor_pd[pd_index];

    /* Verify writable bit (Invariant 1: protected data is read-only) */
    bool unpriv_writable = (unpriv_entry & X86_PTE_WRITABLE);
    bool monitor_writable = (monitor_entry & X86_PTE_WRITABLE);

    serial_puts("VERIFY: Invariant 1 - PTPs read-only in outer kernel:\n");
    serial_puts("VERIFY:   unpriv_pd writable bit: ");
    serial_putc(unpriv_writable ? '1' : '0');
    serial_puts(" (should be 0)\n");

    serial_puts("VERIFY: Invariant 5 - PTPs writable in nested kernel:\n");
    serial_puts("VERIFY:   monitor_pd writable bit: ");
    serial_putc(monitor_writable ? '1' : '0');
    serial_puts(" (should be 1)\n");

    /* === NEW: Verify CR0.WP enforcement === */
    uint64_t cr0;
    asm volatile ("mov %%cr0, %0" : "=r"(cr0));
    bool cr0_wp_enabled = (cr0 & (1 << 16));

    serial_puts("VERIFY: Invariant 2 - CR0.WP enforcement active:\n");
    serial_puts("VERIFY:   CR0.WP bit: ");
    serial_putc(cr0_wp_enabled ? '1' : '0');
    serial_puts(" (should be 1 for outer kernel)\n");

    if (!unpriv_writable && monitor_writable && cr0_wp_enabled) {
        serial_puts("VERIFY: PASS - All Nested Kernel invariants enforced\n");
    } else {
        serial_puts("VERIFY: FAIL - Invariants violated!\n");
    }

    serial_puts("=== Verification Complete ===\n\n");
}
```

---

### Task 5: Add Configuration Option

**File:** `kernel.config` (MODIFY)

Add after CONFIG_WRITE_PROTECTION_VERIFY:

```makefile
# ========================================================================
# Nested Kernel Configuration
# ========================================================================

# Write protection verification - Verify Nested Kernel invariants at boot
# Confirms PTPs are read-only in unprivileged view
# Set to 1 to enable, 0 to disable
CONFIG_WRITE_PROTECTION_VERIFY ?= 1

# CR0.WP control - Enable two-level protection mechanism
# This implements Invariant 2: write-protection permissions are enforced
# Set to 1 to enable CR0.WP manipulation (REQUIRED for full security)
# Set to 0 to disable (PTE-only protection, LESS SECURE)
CONFIG_CR0_WP_CONTROL ?= 1
```

**File:** `Makefile` (MODIFY)

Add CFLAG:

```makefile
CFLAGS += -DCONFIG_WRITE_PROTECTION_VERIFY=$(CONFIG_WRITE_PROTECTION_VERIFY)
CFLAGS += -DCONFIG_CR0_WP_CONTROL=$(CONFIG_CR0_WP_CONTROL)
```

---

### Task 6: Conditional Compilation for CR0.WP Code

**File:** `kernel/main.c` (MODIFY)

Wrap CR0.WP code in conditional:

```c
        /* Switch to unprivileged page tables */
        uint64_t unpriv_cr3 = monitor_get_unpriv_cr3();
        if (unpriv_cr3 != 0) {
            serial_puts("KERNEL: Switching to unprivileged mode\n");

#if CONFIG_CR0_WP_CONTROL
            /* Enable write protection enforcement */
            uint64_t cr0;
            asm volatile ("mov %%cr0, %0" : "=r"(cr0));
            cr0 |= (1 << 16);  /* Set CR0.WP bit */
            asm volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");
            serial_puts("KERNEL: CR0.WP enabled (write protection enforced)\n");
#endif

            asm volatile ("mov %0, %%cr3" : : "r"(unpriv_cr3) : "memory");
            serial_puts("KERNEL: Page table switch complete\n");
        } else {
            serial_puts("KERNEL: Monitor initialization failed\n");
        }
```

**File:** `arch/x86_64/monitor/monitor_call.S` (MODIFY)

Use assembler conditional:

```asm
/* === CR0.WP control for monitor entry/exit === */
#if CONFIG_CR0_WP_CONTROL
    /* Clear CR0.WP for monitor entry */
    mov %cr0, %r11
    and $0xFFFEFFFF, %r11   /* Clear bit 16 (WP) */
    mov %r11, %cr0
#endif

    /* Call C monitor handler */
    call monitor_call_handler

#if CONFIG_CR0_WP_CONTROL
    /* Set CR0.WP for monitor exit */
    mov %cr0, %r11
    or $0x00010000, %r11    /* Set bit 16 (WP) */
    mov %r11, %cr0
#endif
```

---

## Verification Plan

### Build and Test
```bash
# Clean build with CR0.WP control enabled
make clean && make

# Expected: Clean build with no errors
```

### Boot Verification
```bash
make run

# Expected NEW output:
# KERNEL: Switching to unprivileged mode
# KERNEL: CR0.WP enabled (write protection enforced)
# KERNEL: Page table switch complete
#
# === Nested Kernel Invariant Verification ===
# VERIFY: Invariant 1 - PTPs read-only in outer kernel:
# VERIFY:   unpriv_pd writable bit: 0 (should be 0)
# VERIFY: Invariant 5 - PTPs writable in nested kernel:
# VERIFY:   monitor_pd writable bit: 1 (should be 1)
# VERIFY: Invariant 2 - CR0.WP enforcement active:
# VERIFY:   CR0.WP bit: 1 (should be 1 for outer kernel)
# VERIFY: PASS - All Nested Kernel invariants enforced
# === Verification Complete ===
```

### Functional Verification
```bash
# Run all tests
make test-all

# Expected:
# - Basic Boot: PASS
# - APIC Timer: PASS
# - SMP Boot: PASS
```

### Write Protection Test
```bash
# Boot with QEMU and verify writes to PTPs fail in unprivileged mode
# This requires adding a test that attempts to write to monitor pages
# Expected: Page fault (#PF) when outer kernel tries to write to PTPs
```

---

## Alignment with Nested Kernel Paper

### All Invariants Implemented

| Invariant | Implementation | Status After This Plan |
|-----------|----------------|------------------------|
| **Invariant 1**: Protected data is read-only while outer kernel executes | `unpriv_pd` entry has writable bit cleared | ✓ Complete |
| **Invariant 2**: Write-protection permissions are enforced | CR0.WP=1 in outer kernel | ✓ This plan |
| **Invariant 5**: All mappings to PTPs are marked read-only | Monitor page table pages are read-only in unpriv view | ✓ Complete |
| **Invariant 6**: CR3 only loaded with pre-declared PTP | Already implemented (monitor_pml4_phys, unpriv_pml4_phys) | ✓ Complete |

### Paper Design Principles

1. **"Using the MMU to protect the MMU"** ✓
   - PTE permissions mark pages as protected
   - CR0.WP enforces the protection

2. **Nested kernel (monitor) is small and trusted** ✓
   - Monitor controls page table mappings
   - Monitor has CR0.WP=0 to modify PTEs

3. **Outer kernel is de-privileged** ✓
   - Outer kernel has read-only access to page tables
   - CR0.WP=1 prevents write access

4. **Any page table modification goes through monitor** ✓
   - Outer kernel cannot modify page tables (PTE + CR0.WP)
   - Monitor calls switch to privileged mode with CR0.WP=0

---

## Critical Files

| File | Change Type | Purpose |
|------|-------------|---------|
| `kernel/monitor/monitor.c` | MODIFY | Add CR0.WP helper functions |
| `arch/x86_64/monitor/monitor_call.S` | MODIFY | Add CR0.WP entry/exit |
| `kernel/main.c` | MODIFY | Set CR0.WP=1 on unpriv switch |
| `kernel.config` | MODIFY | Add CONFIG_CR0_WP_CONTROL |
| `Makefile` | MODIFY | Add config flag |

---

## Implementation Order

1. Add CR0.WP helper functions to monitor.c
2. Modify monitor_call.S entry/exit with CR0.WP
3. Set CR0.WP=1 in main.c when switching to unprivileged mode
4. Update verification to check CR0.WP
5. Add CONFIG_CR0_WP_CONTROL option
6. Add conditional compilation
7. Build and test
8. Debug and refine

---

## Edge Cases

1. **Monitor call nesting**: Ensure CR0.WP is correctly restored even if monitor calls are nested
2. **Interrupt handling**: CR0.WP must remain set during interrupt handling in outer kernel
3. **Multi-CPU**: Each CPU has its own CR0, no special handling needed
4. **Configuration**: Allow disabling CR0.WP for debugging (less secure)

---

## Success Indicators

1. Build succeeds with CR0.WP code
2. Monitor initialization completes with CR0.WP enabled
3. "CR0.WP enabled" message appears in boot log
4. Verification confirms ALL 3 invariants (1, 2, 5) are enforced
5. Kernel boots successfully to unprivileged mode
6. Monitor calls still work (entry/exit with CR0.WP toggle)
7. Page faults occur when outer kernel tries to write to PTPs

---

## References

**Paper:** "Nested Kernel: An Operating System Architecture for Intra-Kernel Privilege Separation"
- Authors: Nathan Dautenhahn, Theodoros Kasampalis, Will Dietz, John Criswell, Vikram Adve
- Venue: ASPLOS '15
- PDF: [ACM Digital Library](https://dl.acm.org/doi/10.1145/2775054.2694386)
- Website: [nestedkernel.github.io](http://nestedkernel.github.io/)

**Key Sections:**
- Section 4.2: x86-64 Implementation (CR0.WP mechanism)
- Section 5.1: Implementation of Privilege Separation

**Key Quote:**
> "CR0.WP allows the nested kernel to toggle write protection. When entering the
> nested kernel, we clear CR0.WP so the processor ignores write-protect bits in PTEs.
> When returning to the outer kernel, we set CR0.WP so the processor enforces them."
