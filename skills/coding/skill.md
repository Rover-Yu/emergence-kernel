---
name: coding
description: Requirements for source code
---

# Interface Design

1. Interface headers must exist in appropriate include directories.

# Coding Style

1. Use Linux kernel coding style.
2. Use C11 standard.
3. Only use common compiler extensions.
4. Keep compiler warnings at zero (`make` should produce no warnings).

# Memory Safety

1. **Always validate pointers before dereferencing**
   - Check for NULL after allocation functions (pmm_alloc, etc.)
   - Validate addresses from external sources (multiboot, ACPI, hardware)
   - Example: `if (!ptr || (uintptr_t)ptr < 0x100000) { return -1; }`

2. **Add bounds checking for all array access**
   - Validate array indices before use
   - Use `sizeof` / `sizeof(element)` for bounds
   - Example: `if (index < sizeof(array)/sizeof(array[0]))`

3. **Prevent buffer overflows**
   - Use `strnlen` instead of `strlen` for untrusted strings
   - Always null-terminate string buffers
   - Check length before copying: `while (i < sizeof(buf) - 1 && src[i])`

# Concurrency (SMP)

1. **Use memory barriers for multi-CPU synchronization**
   - Call `smp_mb()` after writes that other CPUs need to see
   - Call `smp_mb()` before reads that other CPUs wrote
   - Example: `cpu_ready = true; smp_mb();`

2. **Use atomic operations for shared state**
   - Include `atomic.h` for atomic_fetch_add, atomic_load, etc.
   - Use atomic for CPU index allocation, counters, flags

3. **Lock ordering to prevent deadlocks**
   - Always acquire locks in consistent order
   - Document lock hierarchy if multiple locks exist

# Architecture-Specific Code

1. **Wrap inline assembly in arch-specific functions**
   - Create typed wrapper functions in `arch/x86_64/*.h` headers
   - Never use raw `asm volatile()` in C code (except in wrappers themselves)
   - Benefits: type safety, portability, documentation, maintainability
   - Examples:
     - MSR access: `arch_msr_read(msr)`, `arch_msr_write(msr, val)` in `msr.h`
     - Control regs: `arch_cr0_read()`, `arch_cr3_write(val)` in `cr.h`
     - CPU ops: `arch_halt()`, `arch_cpuid(leaf, ...)` in `cpu.h`
     - TLB ops: `arch_tlb_invalidate_page(addr)` in `paging.h`


# Memory Management

1. **TLB invalidation after page table updates**
   - Use wrapper function: `arch_tlb_invalidate_page(addr)` from `arch/x86_64/paging.h`
   - Required for both kernel and user mappings

2. **Check allocation failure** (pmm_alloc, slab_alloc, etc.)
   - Never assume allocation succeeds
   - Handle NULL return gracefully (error path or halt)

3. **Free resources on error paths**
   - If multiple allocations happen, free earlier ones if later fails
   - Consider cleanup functions or goto cleanup pattern

# Error Handling

1. **Validate external inputs**
   - ACPI tables: validate signatures, lengths, addresses
   - Multiboot info: check sizes before accessing
   - Hardware values: mask to valid range

2. **Return -1 for errors consistently**
   - Functions that fail should return -1 or negative value
   - Callers should check return values

# Build System

1. **Quiet build by default, verbose with V=1**
   - Use `@echo` for short status: `@echo "  CC      $<"`
   - Use `$(Q)` prefix on commands: `$(Q)$(CC) $(CFLAGS)...`
   - Run `make V=1` to see full compiler commands

2. **Zero warnings before committing**
   - All compiler warnings must be fixed before any commit
   - Run `make 2>&1 | grep -E "(warning|error)"` to check for warnings
   - Common warning types to fix:
     - Implicit function declarations → add proper declarations/headers
     - Cast to pointer from integer of different size → use `uintptr_t` intermediate cast
     - Unused variables → remove or mark with `__attribute__((unused))`
     - Signed/unsigned comparison → use matching types or explicit casts

# Source Organization

1. Architecture-specific sources are in folder `arch/${ARCH}`.
2. Architecture-independent core sources are in folder `kernel`.
3. Tests are in folder `tests`,  A test case include two parts,
   e.g. spin lock tests include spinlock_test.c and spinlock_test.sh.
   a) spinlock_test.c : the test functions in kernel, write by C language.
   b) spinlock_test.sh : the script of lauch qemu VM and verify test results by check VM logs.
   c) these two files are in the folder tests/spinlock/.

# Common Pitfalls to Avoid

1. **TOCTOU races** - Time-of-check to time-of-use
   - Check and use of shared state needs synchronization
   - Lock or atomic operations required

2. **Integer overflow**
   - Check before arithmetic: `if (a > UINT64_MAX - b) { error; }`
   - Use unsigned types for sizes/counts

3. **Uninitialized variables**
   - Always initialize variables to known values
   - Enable compiler warnings (-Wall) to catch these
