# KMAP - Kernel Virtual Memory Mapping Subsystem

## Overview

KMAP is a kernel virtual memory mapping subsystem for the Emergence Kernel. It provides tracking and management of kernel memory regions with support for demand paging, SMP-safe reference counting, and nested kernel monitor protection.

## Design Goals

1. **Memory Safety**: Reference-counted lookups prevent use-after-free
2. **Demand Paging**: Pageable regions can allocate pages on fault
3. **SMP Safety**: Atomic operations and spinlocks for multi-CPU correctness
4. **Resource Cleanup**: Empty page tables are freed recursively
5. **Monitor Integration**: Support for nested kernel protection levels

## Architecture

### Core Concepts

**KMAP Entry (`kmap_t`)**
Each memory region with identical mapping properties has one `kmap_t` structure:
- **Virtual address range**: `[virt_start, virt_end)` (page-aligned)
- **Physical backing**: `phys_base` (0 for pageable/alloc-on-demand)
- **Type**: Region classification (CODE, DATA, STACK, etc.)
- **Pageable**: Whether region supports demand paging
- **Monitor protection**: Nested kernel protection level
- **Reference count**: For safe concurrent access
- **Flags**: PTE flags for page table entries

### Region Types

| Type | Description | Pageable | Monitor Protection |
|------|-------------|----------|-------------------|
| `KMAP_CODE` | Executable code (.text) | No | READONLY |
| `KMAP_RODATA` | Read-only data | No | varies |
| `KMAP_DATA` | Read-write data | Yes | varies |
| `KMAP_STACK` | Kernel stacks | No | varies |
| `KMAP_PAGETABLE` | Page table pages | No | varies |
| `KMAP_PMM` | PMM managed pages | Yes | varies |
| `KMAP_SLAB` | Slab allocator pages | Yes | varies |
| `KMAP_DEVICE` | Memory-mapped I/O | No | NONE |
| `KMAP_DYNAMIC` | kmalloc heap | Yes | varies |

### Monitor Protection Levels

| Level | Description |
|-------|-------------|
| `KMAP_MONITOR_NONE` | No special monitor protection |
| `KMAP_MONITOR_READONLY` | Read-only in NK mode (CR0.WP=0) |
| `KMAP_MONITOR_PRIVATE` | Private to monitor (unmapped in OK mode) |

## Public API

### Initialization

```c
void kmap_init(void);
```
Initialize KMAP subsystem and register boot mappings. Must be called after PMM and slab allocator are initialized.

**Boot mappings registered:**
- Identity mapping (0-2MB) - Low memory and MMIO
- Kernel code mapping (KERNEL_BASE_VA+) - Higher-half kernel

### Core Operations

```c
kmap_t *kmap_create(uint64_t virt_start, uint64_t virt_end,
                    uint64_t phys_base, uint64_t flags,
                    kmap_type_t type, kmap_pageable_t pageable,
                    kmap_monitor_t monitor, const char *name);
```
Create a new kernel mapping. Returns pointer to `kmap_t` or NULL on failure.

```c
int kmap_destroy(kmap_t *mapping);
```
Destroy a mapping (must have refcount == 0). Returns 0 on success, negative on failure.

```c
void kmap_get(kmap_t *mapping);
void kmap_put(kmap_t *mapping);
```
Increment/decrement reference count. `kmap_put()` auto-frees when refcount reaches zero.

```c
int kmap_modify_flags(kmap_t *mapping, uint64_t new_flags);
```
Modify PTE flags for all pages in mapping.

```c
int kmap_split(kmap_t *mapping, uint64_t split_addr);
int kmap_merge(kmap_t *m1, kmap_t *m2);
```
Split mapping at address or merge adjacent compatible mappings.

### Lookup

```c
kmap_t *kmap_lookup(uint64_t addr);
kmap_t *kmap_lookup_range(uint64_t start, uint64_t end);
```
Find mapping containing address or overlapping range. Returns mapping with **incremented refcount** - caller must call `kmap_put()` when done.

```c
bool kmap_is_pageable(uint64_t addr);
kmap_monitor_t kmap_get_monitor_protection(uint64_t addr);
```
Query properties of address.

### Page Table Operations

```c
int kmap_map_pages(kmap_t *mapping);
int kmap_unmap_pages(kmap_t *mapping);
```
Map/unmap all pages for a mapping.

### Demand Paging

```c
int kmap_handle_page_fault(uint64_t fault_addr, uint64_t error_code);
```
Handle page fault in pageable region. Called from `page_fault_handler()` in `arch/x86_64/idt.c`.

### Debugging

```c
void kmap_dump(void);
```
Dump all kmap entries to serial console.

## Usage Examples

### Creating a Pageable Data Region

```c
#include "kernel/kmap.h"

// Create 16KB pageable data region at 0x100000000
kmap_t *mapping = kmap_create(
    0x100000000,              // virt_start
    0x100010000,              // virt_end (4 pages)
    0,                         // phys_base (0 = alloc on demand)
    X86_PTE_PRESENT | X86_PTE_WRITABLE,  // flags
    KMAP_DATA,                 // type
    KMAP_PAGEABLE,             // pageable
    KMAP_MONITOR_NONE,         // monitor protection
    "my_data_region"           // name
);

if (mapping == NULL) {
    // Handle allocation failure
}

// Use the mapping...

// Release when done
kmap_put(mapping);  // Auto-frees if refcount reaches zero
```

### Safe Lookup with Reference Counting

```c
// Lookup address (increments refcount)
kmap_t *mapping = kmap_lookup(0x100005000);

if (mapping != NULL) {
    // Check if region is pageable
    if (mapping->pageable == KMAP_PAGEABLE) {
        // Handle pageable region
    }

    // Always release lookup
    kmap_put(mapping);
}
```

### Modifying Mapping Flags

```c
kmap_t *mapping = kmap_lookup(addr);
if (mapping) {
    // Make read-only
    kmap_modify_flags(mapping, X86_PTE_PRESENT);
    kmap_put(mapping);
}
```

### Splitting and Merging

```c
// Split 8-page mapping into two 4-page mappings
kmap_t *mapping = kmap_lookup(base_addr);
if (mapping) {
    uint64_t split_addr = base_addr + (4 * PAGE_SIZE);
    kmap_split(mapping, split_addr);
    kmap_put(mapping);
}

// Later, merge them back (must have identical properties)
kmap_t *m1 = kmap_lookup(base_addr);
kmap_t *m2 = kmap_lookup(base_addr + (4 * PAGE_SIZE));
if (m1 && m2) {
    kmap_merge(m1, m2);
    // m2 is removed from list
    kmap_put(m1);
}
```

## Testing

### Kernel Tests

Run KMAP tests at boot:

```bash
make run KERNEL_CMDLINE='test=kmap'
```

### Integration Tests

Run Python integration tests:

```bash
python3 tests/kmap/kmap_test.py
```

Or via Makefile (not yet added):

```bash
make test-kmap
```

## Test Coverage

The KMAP test suite includes:

### Single-CPU Tests (8 tests)
1. **Boot mappings verification** - Identity and kernel code mappings exist
2. **Create/destroy with refcounting** - Auto-free on zero refcount
3. **Refcounting operations** - kmap_get/kmap_put atomic operations
4. **Lookup operations** - Address and range lookup
5. **Split operation** - Divide mapping at boundary
6. **Merge operation** - Combine adjacent compatible mappings
7. **Modify flags** - Update PTE flags for all pages
8. **Pageable status query** - kmap_is_pageable and monitor protection

### SMP Tests (1 test, requires 2+ CPUs)
9. **SMP concurrent lookup** - Multi-CPU race condition handling

**Total: 9 comprehensive tests**

All tests pass successfully, covering edge cases, SMP scenarios, and integration with demand paging.

## Configuration

KMAP tests are controlled by the `CONFIG_TESTS_KMAP` configuration option in `kernel.config`:

```makefile
# KMAP tests - Test kernel virtual memory mapping subsystem
# Set to 1 to enable, 0 to disable (default: enabled)
CONFIG_TESTS_KMAP ?= 1
```

## Implementation Details

### SMP Safety

The KMAP subsystem uses several mechanisms for SMP safety:

1. **Spinlock protection**: `kmap_lock` protects the global kmap list
2. **Atomic refcounting**: Uses `__atomic_add_fetch()` and `__atomic_fetch_sub()`
3. **IRQ-safe operations**: `spin_lock_irqsave()` / `spin_unlock_irqrestore()`

```c
// Atomic reference increment
void kmap_get(kmap_t *mapping) {
    if (mapping != NULL) {
        __atomic_add_fetch(&mapping->refcount, 1, __ATOMIC_SEQ_CST);
    }
}
```

### Page Table Cleanup

When unmapping pages, the KMAP subsystem recursively cleans up empty page tables:

```c
kmap_free_page(addr):
    1. Unmap the page
    2. Check if PT (Page Table) is empty
    3. If empty, free PT and check if PD (Page Directory) is empty
    4. If empty, free PD and check if PDPT is empty
    5. If empty, free PDPT
```

This prevents memory leaks from page table structures.

### Demand Paging Flow

```
Page Fault (CR2)
    ↓
page_fault_handler() [arch/x86_64/idt.c]
    ↓
kmap_handle_page_fault()
    ↓
kmap_lookup(fault_addr)
    ↓
Check if pageable
    ↓
Check if already mapped (race detection)
    ↓
kmap_alloc_page() [allocate and map]
    ↓
Return to execution
```

### Race Condition Detection

The demand paging handler detects race conditions when multiple CPUs fault on the same page:

```c
// Check if page is already mapped (race with another CPU)
bool already_mapped = false;
if ((pml4[pml4_idx] & X86_PTE_PRESENT)) {
    // Walk page tables...
    if ((pt[pt_idx] & X86_PTE_PRESENT)) {
        already_mapped = true;
    }
}

if (already_mapped) {
    // Another CPU already mapped this page
    klog_info("KMAP", "page already mapped (race detected)");
    return 0;  // Success - page is mapped
}
```

## Files

- `kernel/kmap.h` - Public API and data structures
- `kernel/kmap.c` - Core implementation (~1000 lines)
- `arch/x86_64/idt.c` - Page fault handler with KMAP support
- `arch/x86_64/main.c` - KMAP initialization
- `tests/kmap/kmap_test.c` - Kernel test suite
- `tests/kmap/test_kmap.h` - Test wrapper header
- `tests/kmap/kmap_test.py` - Python integration test

## Dependencies

KMAP requires the following subsystems:
- **PMM** - Physical memory page allocation
- **Slab allocator** - kmap_t structure allocation
- **Spinlock** - List protection
- **klog** - Logging output

## Integration Points

The KMAP subsystem is integrated with:

1. **Page fault handler** (`arch/x86_64/idt.c`) - Demand paging support
2. **Monitor subsystem** - Protection level tracking for nested kernel
3. **Boot sequence** (`arch/x86_64/main.c`) - Initialized after slab allocator

## Future Enhancements

Potential additions to the KMAP subsystem:

- **Statistics tracking**: Add counters for allocations, deallocations, page faults
- **Debug commands**: Runtime commands to inspect mappings via kernel shell
- **Permission checking**: Enhanced validation before page table modifications
- **Performance monitoring**: Track lookup latency, contention hotspots
- **Integration with VM**: Work with process address spaces for user memory

## See Also

- `docs/MEMORY_MAPPINGS.md` - Overall memory layout design
- `docs/monitor_trampoline.md` - Nested kernel trampoline design
- `kernel/vm.c` - Virtual memory management (future per-process page tables)
