# Nested Kernel Fixes and Improvements Design

**Date:** 2026-02-14
**Status:** Approved
**Scope:** P1-P3 fixes from design review (10 fixes total)

---

## Overview

This design document captures the implementation approach for fixing security vulnerabilities and improving code quality in the Nested Kernel implementation. The fixes are derived from the comprehensive design review documented in `docs/reviews/nk-design-review-report.md`.

### Goals

1. **P1 Security:** Fix 3 critical security issues (SMP race, incomplete protection, disabled WP)
2. **P2 Functionality:** Implement 4 functional improvements (mapping, tracking, tests)
3. **P3 Quality:** Apply 3 code quality improvements (error handling, dead code)

### Non-Goals

- P4 long-term items (VMX/EPT, guard pages) - future work
- API changes or new features beyond fixing existing gaps
- Performance optimizations

---

## Architecture

### Modified Files Summary

| File | Changes | Priority | Risk |
|------|---------|----------|------|
| `arch/x86_64/monitor/monitor_call.S` | Per-CPU saved_rsp/saved_cr3 via GS-base | P1 | High |
| `arch/x86_64/smp.c` | Set GS base during AP init | P1 | Medium |
| `arch/x86_64/main.c` | Enable BSP CR0.WP, set GS base for BSP | P1 | Medium |
| `kernel/monitor/monitor.c` | Complete PTP protection, implement mapping | P1, P2 | High |
| `kernel/slab.c` | Use monitor_pmm_alloc | P2 | Low |
| `kernel/pcd.c` | Add error returns, track PCD array | P2, P3 | Low |
| `kernel/pcd.h` | Remove unused refcount field | P3 | Low |
| `tests/smp_monitor_stress/smp_monitor_stress_test.c` | New file - SMP stress tests | P2 | Low |
| `tests/nested_kernel_invariants/nested_kernel_invariants_test.c` | Add negative tests | P2 | Low |

---

## P1 Security Fixes

### Fix 1: Per-CPU Trampoline Data via GS-Base

**Problem:** Global `saved_rsp` and `saved_cr3` variables cause race conditions on SMP systems.

**Solution:** Use GS segment base for per-CPU data access.

**Data Structure:**
```c
// In arch/x86_64/smp.h
typedef struct {
    uint64_t saved_rsp;     // Offset 0
    uint64_t saved_cr3;     // Offset 8
    int cpu_index;          // Offset 16
} per_cpu_data_t;

extern per_cpu_data_t per_cpu_data[SMP_MAX_CPUS];
```

**Assembly Changes:**
```asm
// Replace RIP-relative with GS-relative addressing
mov %rsp, %gs:0     // saved_rsp at offset 0
mov %cr3, %rax
mov %rax, %gs:8     // saved_cr3 at offset 8

// Restore:
mov %gs:8, %rax
mov %rax, %cr3
mov %gs:0, %rsp
```

**GS-Base Setup:**
```c
// In smp.c and main.c
void set_gs_base(per_cpu_data_t *cpu_data) {
    uint64_t addr = (uint64_t)cpu_data;
    asm volatile ("wrmsr" : :
        "c"(0xC0000101),  // IA32_GS_BASE MSR
        "a"((uint32_t)addr),
        "d"((uint32_t)(addr >> 32)));
}
```

**Files:** `monitor_call.S`, `smp.c`, `main.c`, `smp.h`

---

### Fix 2: Complete PTP Protection

**Problem:** Only one 2MB region is protected; `monitor_pt_0_2mb` and `unpriv_pt_0_2mb` are missing.

**Solution:** Use PCD-based discovery to protect all NK_PGTABLE pages.

**Algorithm:**
```c
static void monitor_protect_all_ptps(void) {
    serial_puts("MONITOR: Protecting all PTPs via PCD discovery\n");
    int protected_count = 0;

    for (uint64_t i = 0; i < pcd_get_max_pages(); i++) {
        uint64_t phys = i << PAGE_SHIFT;
        uint8_t type = pcd_get_type(phys);

        if (type == PCD_TYPE_NK_PGTABLE) {
            int pte_idx = phys >> 12;
            if (pte_idx < 512) {  // First 2MB region
                unpriv_pt_0_2mb[pte_idx] &= ~X86_PTE_WRITABLE;
            }
            protected_count++;
        }
    }

    // Invalidate TLB for all protected pages
    for (int i = 0; i < 512; i++) {
        if (!(unpriv_pt_0_2mb[i] & X86_PTE_WRITABLE)) {
            monitor_invalidate_page((void*)(i << 12));
        }
    }

    serial_puts("MONITOR: Protected ");
    serial_put_hex(protected_count);
    serial_puts(" PTP pages\n");
}
```

**Files:** `monitor.c`

---

### Fix 3: Enable BSP CR0.WP

**Problem:** BSP CR0.WP setting is inside `#if 0` block, disabled for testing.

**Solution:** Enable with conditional for usermode tests only.

**Change:**
```c
// In main.c, replace #if 0 with:
#if !defined(CONFIG_TESTS_USERMODE) || !CONFIG_TESTS_USERMODE
    // CR3 switch and CR0.WP code
#endif
```

**Files:** `main.c`

---

## P2 Functional Improvements

### Fix 4: PCD Tracking Fix

**Problem:** Slab allocator calls `pmm_alloc()` directly, bypassing PCD tracking.

**Solution:** Use `monitor_pmm_alloc()` wrapper.

**Change:**
```c
// In slab.c:66
// Before:
page_addr = pmm_alloc(0);

// After:
page_addr = monitor_pmm_alloc(0);
```

**Files:** `slab.c`

---

### Fix 5: Implement Page Mapping

**Problem:** `monitor_map_page()` validates but never creates actual mappings.

**Solution:** Implement full page table walking.

**Implementation:**
```c
static uint64_t *get_or_create_table(uint64_t *entry, bool *allocated) {
    if (*entry & X86_PTE_PRESENT) {
        *allocated = false;
        return (uint64_t*)(*entry & ~0xFFF);
    }

    uint64_t *table = monitor_alloc_pgtable(0);
    if (!table) return NULL;

    *entry = virt_to_phys(table) | X86_PTE_PRESENT | X86_PTE_WRITABLE;
    *allocated = true;
    return table;
}

int monitor_map_page(uint64_t phys_addr, uint64_t virt_addr, uint64_t flags) {
    // 1. PCD validation (existing)
    if (validate_pcd(phys_addr, flags) < 0) return -1;

    // 2. Walk page tables
    int pml4_idx = PML4_INDEX(virt_addr);
    int pdpt_idx = PDPT_INDEX(virt_addr);
    int pd_idx = PD_INDEX(virt_addr);
    int pt_idx = PT_INDEX(virt_addr);

    uint64_t *pdpt = get_or_create_table(&unpriv_pml4[pml4_idx], NULL);
    uint64_t *pd = get_or_create_table(&pdpt[pdpt_idx], NULL);
    uint64_t *pt = get_or_create_table(&pd[pd_idx], NULL);

    if (!pdpt || !pd || !pt) return -1;

    // 3. Set PTE
    pt[pt_idx] = phys_addr | flags | X86_PTE_PRESENT;

    // 4. Invalidate TLB
    monitor_invalidate_page((void*)virt_addr);

    return 0;
}
```

**Files:** `monitor.c`

---

### Fix 6: SMP Stress Tests

**New test file to verify concurrent monitor calls work correctly.**

**Test Scenarios:**
1. Concurrent allocations from all CPUs
2. Concurrent free operations
3. Mixed alloc/free patterns
4. Memory integrity verification after stress

**Files:** New `tests/smp_monitor_stress/`

---

### Fix 7: Negative Invariant Tests

**Add tests that intentionally violate invariants to verify detection.**

**Test Cases:**
1. PTP write detection
2. Arbitrary CR3 load detection
3. Writable NK mapping rejection

**Files:** `tests/nested_kernel_invariants/`

---

## P3 Code Quality

### Fix 8: PCD Error Handling

**Changes:**
- `pcd_set_type()`: Return int instead of void, -1 on error
- `pcd_mark_region()`: Return count of pages marked
- Add warning messages for unmanaged addresses

**Files:** `pcd.c`, `pcd.h`

---

### Fix 9: Remove Unused Refcount Field

**Change:**
```c
// Remove uint32_t refcount from pcd_t
// Saves 4 bytes per page (1MB per 1GB RAM)
```

**Files:** `pcd.h`, `pcd.c`

---

## Testing Strategy

### Incremental Testing

Each fix must pass `make test-all` before proceeding:

```
Phase A (P1):
  1. GS-base setup → make test-all
  2. Trampoline changes → make test-all
  3. PTP protection → make test-all
  4. BSP CR0.WP → make test-all

Phase B (P2):
  5. Slab fix → make test-all
  6. Page mapping → make test-all
  7. SMP stress tests → make test-all
  8. Negative tests → make test-all

Phase C (P3):
  9. PCD error handling → make test-all
  10. Refcount removal → make test-all
```

### Verification Commands

```bash
# Run all tests
make test-all

# Run specific NK tests
make run KERNEL_CMDLINE='test=nested_kernel_invariants'
make run KERNEL_CMDLINE='test=pcd'

# Run SMP stress test (after implementation)
make run KERNEL_CMDLINE='test=smp_monitor_stress'
```

---

## Risk Assessment

| Fix | Risk | Mitigation |
|-----|------|------------|
| Per-CPU GS-base | High - assembly changes | Test on all CPUs, verify no crashes |
| PTP protection | Medium - page table changes | Run fault injection tests |
| BSP CR0.WP | Low - simple enable | Verify usermode tests still work |
| Page mapping | Medium - new code | Extensive unit testing |

---

## Success Criteria

1. All `make test-all` tests pass after each fix
2. No new compiler warnings
3. SMP stress tests pass with 2+ CPUs
4. Invariant verification still passes
5. Fault injection tests still catch violations

---

## References

- Design Review: `docs/reviews/nk-design-review-report.md`
- Paper Compliance: `docs/reviews/nk-paper-compliance.md`
- Trampoline Docs: `docs/monitor_trampoline.md`
- API Docs: `docs/monitor_api.md`
