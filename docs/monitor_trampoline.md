# Monitor Entry Trampoline

This document describes the monitor entry trampoline mechanism that enables secure privilege transitions in the Nested Kernel architecture.

## Overview

The `nk_entry_trampoline` is an assembly stub that performs a controlled CR3 switch to transition from unprivileged (outer kernel) mode to privileged (monitor) mode. This trampoline is the core of the Nested Kernel's inter-domain communication mechanism, allowing the outer kernel to request privileged operations while maintaining strict privilege separation.

### Purpose

The trampoline solves a fundamental problem in the Nested Kernel architecture:
- The outer kernel runs with restricted page tables (`unpriv_pml4_phys`) where page table pages (PTPs) are read-only
- The monitor (nested kernel) runs with full page tables (`monitor_pml4_phys`) where PTPs are writable
- The outer kernel needs to request privileged operations (e.g., memory allocation) from the monitor
- Direct CR3 manipulation from unprivileged code would be a security violation

The trampoline provides a controlled entry point that:
1. Validates the transition is occurring through the proper mechanism
2. Saves all processor state
3. Switches to a stack accessible in the privileged view
4. Switches CR3 to the privileged page tables
5. Executes the requested operation
6. Restores the original state and returns to unprivileged mode

## Memory Layout Requirements

### Identity Mapping Requirement

The trampoline code and data must be identity-mapped in BOTH page table views:

```
Virtual Address  |  Monitor View      |  Unprivileged View
-----------------|--------------------|----------------------
0x100000 + N     |  nk_entry_trampoline  |  nk_entry_trampoline
saved_rsp        |  Per-CPU RSP         |  Per-CPU RSP
nk_boot_stack_top|  Monitor stack       |  (inaccessible)
```

**Critical Requirement:** The trampoline uses RIP-relative addressing to access `saved_rsp`, which means the address of `saved_rsp` is computed relative to `RIP`. Since `nk_entry_trampoline` is identity-mapped in both page tables, `saved_rsp(%rip)` resolves to the same physical location regardless of which CR3 is active.

### Stack Management

The trampoline uses two separate stacks:

1. **Unprivileged Stack**: The outer kernel's normal stack, where the call originates
   - Saved in `saved_rsp` before CR3 switch
   - Must be identity-mapped in both page tables

2. **Monitor Stack**: `nk_boot_stack_top`, used while executing in privileged mode
   - Only mapped in the monitor page tables
   - Prevents unprivileged code from accessing monitor stack data

### Per-CPU Data

Current implementation uses a single `saved_rsp` variable shared by all CPUs:

```assembly
.section .bss
.align 8
.global saved_rsp
saved_rsp:
    .quad 0
```

**Current Limitation:** This is NOT per-CPU. Concurrent monitor calls from multiple CPUs will corrupt `saved_rsp`. For multi-CPU systems, `saved_rsp` must be in a per-CPU data area (e.g., using GS-base addressing).

## CR3 Switching Protocol

### Trampoline Entry and Exit

```
┌─────────────────────────────────────────────────────────────────┐
│  Unprivileged Mode (outer kernel)                               │
│  CR3 = unpriv_pml4_phys                                         │
│  Stack = unprivileged stack                                     │
└─────────────────────────────────────────────────────────────────┘
                            │
                            ▼
                    monitor_call() invoked
                            │
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│  nk_entry_trampoline                                            │
├─────────────────────────────────────────────────────────────────┤
│  1. Save registers (rbx, rbp, r12-r15)                          │
│  2. Save RSP → saved_rsp(%rip)  [identity-mapped]              │
│  3. Read CR3 → r8  [unpriv_pml4_phys]                          │
│  4. Load monitor_pml4_phys → r9                                 │
│  5. Switch CR3: r9 → CR3  [NOW PRIVILEGED]                     │
│  6. Switch RSP → nk_boot_stack_top - 128                        │
│  7. Call monitor_call_handler(call, arg1, arg2, arg3)          │
│  8. Restore CR3: r8 → CR3  [BACK TO UNPRIVILEGED]              │
│  9. Restore RSP ← saved_rsp(%rip)                               │
│ 10. Restore registers                                           │
│ 11. Return to caller                                            │
└─────────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│  Unprivileged Mode (outer kernel)                               │
│  CR3 = unpriv_pml4_phys (restored)                              │
│  Stack = unprivileged stack (restored)                          │
└─────────────────────────────────────────────────────────────────┘
```

### Stack Diagram

```
Unprivileged Mode              Privileged Mode (during trampoline)
┌────────────────────┐         ┌─────────────────────────────────┐
│   caller's frame   │         │   nk_boot_stack_top             │
├────────────────────┤         ├─────────────────────────────────┤
│   monitor_call()   │         │   (128 bytes reserved)          │
│   frame            │         │   ├─ Return address             │
├────────────────────┤         │   ├─ Saved r15                  │
│   ret addr (to     │         │   ├─ Saved r14                  │
│   monitor_call)    │         │   ├─ Saved r13                  │
├────────────────────┤         │   ├─ Saved r12                  │
│   trampoline       │         │   ├─ Saved rbp                  │
│   saves registers  │         │   ├─ Saved rbx                  │
├────────────────────┤         │   ├─ Saved RSP (from unpriv)    │
│   saved_rsp points │         │   └─ Local variables           │
│   here             │         └─────────────────────────────────┘
└────────────────────┘
```

## Security Properties

### Privilege Separation

The trampoline enforces strict privilege boundaries:

1. **Controlled Entry Point:** The only way to enter privileged mode is through `nk_entry_trampoline`
2. **No Direct CR3 Access:** The outer kernel never directly manipulates CR3
3. **Stack Isolation:** Monitor stack is only accessible in privileged mode
4. **State Restoration:** The outer kernel's state is fully restored before return

### Invariant Enforcement

The trampoline helps maintain Nested Kernel invariants:

| Invariant | Trampoline Role |
|-----------|-----------------|
| **Inv 1** (PTPs read-only in outer kernel) | Trampoline prevents outer kernel from gaining write access |
| **Inv 2** (Write protection enforced) | Trampoline entry/exit doesn't bypass CR0.WP |
| **Inv 4** (Context switch available) | Trampoline IS the context switch mechanism |
| **Inv 6** (CR3 pre-declared) | Trampoline only switches to `monitor_pml4_phys` |

### Attack Mitigation

The trampoline design mitigates several attack vectors:

1. **Stack Smashing:** Monitor stack is isolated from unprivileged access
2. **CR3 Hijacking:** Outer kernel cannot load arbitrary CR3 values
3. **Privilege Escalation:** All privileged code goes through the handler
4. **State Corruption:** Full register save/restore prevents state leakage

## Implementation Details

### Assembly Code Location

File: `arch/x86_64/monitor/monitor_call.S`

Key sections:
- `.text` - Contains `nk_entry_trampoline` and debug functions
- `.bss` - Contains `saved_rsp` (must be identity-mapped)
- `.rodata` - Contains debug messages

### Calling Convention

The trampoline follows the System V AMD64 ABI with modifications for privilege transition:

**Input (in registers):**
- `rdi` - `monitor_call_t call` (monitor call type)
- `rsi` - `uint64_t arg1`
- `rdx` - `uint64_t arg2`
- `rcx` - `uint64_t arg3`

**Output (in registers):**
- `rax` - `monitor_ret_t.result`
- `rdx` - `monitor_ret_t.error`

**Preserved:**
- All callee-saved registers (rbx, rbp, r12-r15)
- Original RSP (restored from `saved_rsp`)

**Clobbered:**
- r8, r9, r10, r11 (temporary use during trampoline)

### C Wrapper

File: `kernel/monitor/monitor.c`

```c
monitor_ret_t monitor_call(monitor_call_t call, uint64_t arg1,
                            uint64_t arg2, uint64_t arg3) {
    /* If monitor not initialized yet, call directly */
    if (monitor_pml4_phys == 0) {
        return monitor_call_handler(call, arg1, arg2, arg3);
    }

    /* Already privileged? Call directly */
    if (monitor_is_privileged()) {
        return monitor_call_handler(call, arg1, arg2, arg3);
    }

    /* Unprivileged: use assembly stub to switch CR3 */
    return nk_entry_trampoline(call, arg1, arg2, arg3);
}
```

## Testing

### Test Location

File: `tests/monitor_trampoline/monitor_trampoline_test.c`

### Test Coverage

The test verifies:
1. Initial state is unprivileged
2. Allocation through monitor call succeeds
3. Return to unprivileged mode after call
4. Free operation succeeds
5. Multiple consecutive calls work correctly
6. Final state remains unprivileged

### Running the Test

Enable with `CONFIG_MONITOR_TRAMPOLINE_TEST=1` in `kernel.config`:

```bash
# Enable test
echo "CONFIG_MONITOR_TRAMPOLINE_TEST=y" >> kernel.config

# Build and run
make
make run

# Expected output:
# [TRAMPOLINE TEST] Starting monitor call test
# [TRAMPOLINE TEST] Confirmed: Running in unprivileged mode
# [TRAMPOLINE TEST] Test 1: Allocate page via monitor_call
# [TRAMPOLINE TEST] PASS: Allocation succeeded, addr = 0x...
# [TRAMPOLINE TEST] PASS: Returned to unprivileged mode
# [TRAMPOLINE TEST] PASS: Free succeeded
# [TRAMPOLINE TEST] Test 4: Multiple allocations
# [TRAMPOLINE TEST] PASS: All 3 allocations succeeded
# [TRAMPOLINE TEST] PASS: All allocations freed
# [TRAMPOLINE TEST] PASS: Still in unprivileged mode
# [TRAMPOLINE TEST] All tests PASSED
```

### Integration Testing

The trampoline is also tested indirectly through:
- PMM allocation tests (`CONFIG_PMM_TESTS`)
- Nested kernel protection tests (`CONFIG_NK_PROTECTION_TESTS`)

## Known Limitations

### Per-CPU Data

Current implementation uses a single `saved_rsp` for all CPUs. This works for single-CPU systems but will fail with multiple CPUs making concurrent monitor calls.

**Solution:** Use per-CPU data areas:
```assembly
/* Use GS-base for per-CPU data */
mov %gs:saved_rsp_offset, %rsp
```

### Stack Size

The monitor stack (`nk_boot_stack_top`) is 16KB. Heavy nested calls or large local variables in monitor code could overflow this stack.

**Solution:** Increase stack size or implement stack checking.

### Debug Overhead

The trampoline includes debug output (`debug_putc`) that adds overhead. Production builds should disable this.

## Future Enhancements

1. **Per-CPU Trampoline Data:** Implement proper per-CPU `saved_rsp` storage
2. **Stack Overflow Protection:** Add guard pages and stack checking
3. **Performance Optimization:** Reduce register save/restore overhead for hot paths
4. **Formal Verification:** Model the trampoline in verification tools
5. **Alternative Mechanisms:** Explore SYSCALL/SYSRET or interrupt gates for entry

## References

- **Paper:** "Nested Kernel: An Operating System Architecture for Intra-Kernel Privilege Separation" (ASPLOS '15)
- **Related Docs:** `docs/monitor_api.md` - Monitor API reference
- **Implementation:** `arch/x86_64/monitor/monitor_call.S` - Trampoline assembly
- **Tests:** `tests/monitor_trampoline/monitor_trampoline_test.c` - Test suite
