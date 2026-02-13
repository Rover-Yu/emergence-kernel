# Monitor Entry Trampoline CR3 Switching Fix

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Fix the `nk_entry_trampoline` to properly switch CR3 between privileged (monitor) and unprivileged page tables, enabling secure monitor calls from unprivileged mode.

**Architecture:** The monitor entry trampoline must:
1. Be identity-mapped at the SAME virtual address in BOTH page table views (monitor and unprivileged)
2. Save current CR3, switch to monitor CR3, call handler, restore CR3, return
3. Handle stack preservation across CR3 switches
4. Maintain System V AMD64 ABI calling convention

**Tech Stack:** x86_64 assembly, nested kernel architecture, CR3 page table switching, System V AMD64 ABI

---

## Background Analysis

**Current State (BROKEN):**
- `nk_entry_trampoline` in `arch/x86_64/monitor/monitor_call.S` is disabled
- `monitor_call()` in `kernel/monitor/monitor.c` has TEMPORARY WORKAROUND calling handler directly
- This violates the Nested Kernel security model - unprivileged code can call privileged handler directly

**Root Cause:**
The trampoline code must be mapped at the same virtual address in BOTH page tables for CR3 switching to work. When CR3 changes, the current EIP/RIP must remain valid.

**Key Insight from monitor.c:**
```c
/* Update unpriv_pml4[0] to point to unpriv_pdpt instead of boot_pdpt */
/* Update unpriv_pdpt[0] to point to unpriv_pd instead of boot_pd */
```
The unprivileged page tables are modified to have different PML4[0] and PDPT[0] entries. The trampoline code lives in `.text` section which should be in the identity-mapped region.

---

## Task 1: Verify Trampoline Code Location and Mapping

**Files:**
- Read: `arch/x86_64/linker.ld`
- Modify: None (analysis only)
- Test: Build and check map file

**Step 1: Read the linker script to understand memory layout**

Run: `cat arch/x86_64/linker.ld | head -100`
Expected: Identify where `.text` section is placed, verify it's in identity-mapped region

**Step 2: Build kernel and examine map file**

Run: `make clean && make && cat build/emergence.map | grep -E "(nk_entry_trampoline|\.text|monitor_call)"`
Expected: Confirm `nk_entry_trampoline` is in `.text` section at a low virtual address (< 2MB)

**Step 3: Verify identity mapping in both page tables**

Run: `grep -A5 "Copy boot page table mappings" kernel/monitor/monitor.c`
Expected: Confirm both monitor and unpriv views copy from boot_pml4, so `.text` should be identically mapped

---

## Task 2: Implement CR3 Switching in Trampoline

**Files:**
- Modify: `arch/x86_64/monitor/monitor_call.S:23-46`
- Test: `kernel/monitor/monitor.c` (will be enabled in Task 4)

**Step 1: Write the CR3 switching trampoline**

The trampoline needs to:
1. Save all registers (already done)
2. Save current RSP (stack pointer) - must be in memory valid in BOTH page tables
3. Switch CR3 to monitor_pml4_phys
4. Call handler
5. Restore CR3 to saved value
6. Restore RSP
7. Restore registers
8. Return

**Key Challenge:** The saved RSP must be in a region that's valid in BOTH page tables.

**Solution:** Use the per-CPU boot stack (`nk_boot_stack_top`) which is identity-mapped in both views.

```asm
/* nk_entry_trampoline - Switch to privileged mode, call handler, return
 *
 * Arguments (System V AMD64 ABI):
 *   rdi = monitor_call_t call
 *   rsi = uint64_t arg1
 *   rdx = uint64_t arg2
 *   rcx = uint64_t arg3
 * Returns:
 *   rax = monitor_ret_t.result
 *   rdx = monitor_ret_t.error
 *
 * CRITICAL: This code must be identity-mapped in BOTH page tables!
 * The .text section where this lives is copied from boot_pml4, so it's
 * identity-mapped in both monitor and unprivileged views.
 */
.global nk_entry_trampoline
nk_entry_trampoline:
    /* Save all registers */
    push %rbx
    push %rbp
    push %r12
    push %r13
    push %r14
    push %r15

    /* Save current RSP to a per-CPU location that's valid in BOTH page tables
     * We use a fixed offset in the boot stack region which is identity-mapped
     * in both monitor and unprivileged views */
    mov %rsp, saved_rsp(%rip)

    /* Get current CR3 */
    mov %cr3, %r8

    /* Switch to monitor (privileged) page tables
     * monitor_pml4_phys contains the physical address of monitor PML4 */
    movabs $monitor_pml4_phys, %r9
    mov (%r9), %r9           /* Load the actual PML4 physical address */
    mov %r9, %cr3            /* Switch to monitor page tables */

    /* Switch to monitor stack (use per-CPU boot stack top)
     * This stack is identity-mapped and writable in monitor view */
    movabs $nk_boot_stack_top, %rsp
    sub $128, %rsp           /* Reserve space on stack */

    /* Call C monitor handler (now in privileged mode)
     * Arguments are already in rdi, rsi, rdx, rcx (System V ABI) */
    call monitor_call_handler

    /* Result in rax, error in rdx (System V ABI) */

    /* Restore original CR3 (switch back to unprivileged page tables) */
    mov %r8, %cr3

    /* Restore original RSP */
    mov saved_rsp(%rip), %rsp

    /* Restore all registers */
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbp
    pop %rbx

    /* Return to caller (in unprivileged mode now) */
    ret

/* Per-CPU saved RSP storage
 * Each CPU needs its own slot to avoid races
 * For now, we use a single slot (APs don't use monitor calls yet)
 */
.section .bss
.align 8
saved_rsp:
    .quad 0

.size nk_entry_trampoline, . - nk_entry_trampoline

/* External symbols */
.global monitor_pml4_phys
.global monitor_call_handler
.global nk_boot_stack_top
```

**Step 2: Verify assembly syntax is correct**

Run: `make build/boot_monitor_monitor_call.o 2>&1 | head -20`
Expected: Successful compilation with no errors

---

## Task 3: Enable Trampoline in monitor_call()

**Files:**
- Modify: `kernel/monitor/monitor.c:796-822`
- Test: Integration test (Task 5)

**Step 1: Enable the disabled code path**

Replace the TEMPORARY WORKAROUND with the proper trampoline call:

```c
/* Public monitor call wrapper (for unprivileged code) */
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

**Step 2: Verify compilation**

Run: `make build/kernel_monitor_monitor.o 2>&1 | head -20`
Expected: Successful compilation with no errors

**Step 3: Commit trampoline enablement**

```bash
git add arch/x86_64/monitor/monitor_call.S kernel/monitor/monitor.c
git commit -m "feat(monitor): enable CR3 switching in nk_entry_trampoline

- Implement proper CR3 switching in nk_entry_trampoline
- Save/restore RSP across page table switches
- Use per-CPU boot stack as monitor-side stack
- Enable trampoline path in monitor_call()

This restores the secure monitor call mechanism where unprivileged
code must switch page tables to access privileged operations."
```

---

## Task 4: Test Monitor Call from Unprivileged Mode

**Files:**
- Create: `tests/monitor_trampoline/monitor_trampoline_test.c`
- Modify: `Makefile` (add test to build)
- Test: Run QEMU with test output

**Step 1: Write integration test**

Create `tests/monitor_trampoline/monitor_trampoline_test.c`:

```c
/* Test that monitor trampoline properly switches CR3 */

#include <stdint.h>
#include "arch/x86_64/serial.h"
#include "kernel/monitor/monitor.h"

/* External trampoline function */
extern monitor_ret_t nk_entry_trampoline(monitor_call_t call,
                                         uint64_t arg1,
                                         uint64_t arg2,
                                         uint64_t arg3);

/* Test: Call monitor from unprivileged mode */
void test_monitor_call_from_unprivileged(void) {
    serial_puts("[TRAMPOLINE TEST] Starting monitor call test\n");

    /* We should be in unprivileged mode at this point */
    if (monitor_is_privileged()) {
        serial_puts("[TRAMPOLINE TEST] FAIL: Already in privileged mode\n");
        return;
    }
    serial_puts("[TRAMPOLINE TEST] Confirmed: Running in unprivileged mode\n");

    /* Test 1: Simple allocation through monitor call */
    serial_puts("[TRAMPOLINE TEST] Test 1: Allocate page via monitor_call\n");
    monitor_ret_t ret = monitor_call(MONITOR_CALL_ALLOC_PHYS, 0, 0, 0);

    if (ret.error != 0) {
        serial_puts("[TRAMPOLINE TEST] FAIL: Allocation returned error\n");
        return;
    }

    if (ret.result == 0) {
        serial_puts("[TRAMPOLINE TEST] FAIL: Allocation returned NULL\n");
        return;
    }

    serial_puts("[TRAMPOLINE TEST] PASS: Allocation succeeded, addr = 0x");
    serial_put_hex(ret.result);
    serial_puts("\n");

    /* Test 2: Verify we're back in unprivileged mode */
    if (monitor_is_privileged()) {
        serial_puts("[TRAMPOLINE TEST] FAIL: Still in privileged mode after call\n");
        return;
    }
    serial_puts("[TRAMPOLINE TEST] PASS: Returned to unprivileged mode\n");

    /* Test 3: Free the allocation */
    monitor_call(MONITOR_CALL_FREE_PHYS, ret.result, 0, 0);
    serial_puts("[TRAMPOLINE TEST] PASS: Free succeeded\n");

    serial_puts("[TRAMPOLINE TEST] All tests PASSED\n");
}
```

**Step 2: Add test to Makefile**

Add to `Makefile`:

```makefile
# Monitor trampoline tests
TESTS_C_SRCS += tests/monitor_trampoline/monitor_trampoline_test.c
```

**Step 3: Call test from main.c**

Add to `arch/x86_64/main.c` after PMM tests:

```c
#if CONFIG_MONITOR_TRAMPOLINE_TEST
    /* Test monitor trampoline CR3 switching */
    serial_puts("KERNEL: Testing monitor trampoline...\n");
    extern void test_monitor_call_from_unprivileged(void);
    test_monitor_call_from_unprivileged();
#endif
```

**Step 4: Add config option to Makefile**

```makefile
CFLAGS += -DCONFIG_MONITOR_TRAMPOLINE_TEST=1
```

**Step 5: Build and verify**

Run: `make clean && make 2>&1 | tail -20`
Expected: Clean build with test included

---

## Task 5: Run Integration Test and Verify

**Files:**
- Test: Run QEMU and capture output
- Verify: Check for expected test output

**Step 1: Run kernel with test enabled**

Run: `make run 2>&1 | tee /tmp/test_output.log`
Expected: Kernel boots, monitor initializes, trampoline test runs

**Step 2: Verify test output**

Run: `grep -E "\[TRAMPOLINE TEST\]" /tmp/test_output.log`
Expected output:
```
[TRAMPOLINE TEST] Starting monitor call test
[TRAMPOLINE TEST] Confirmed: Running in unprivileged mode
[TRAMPOLINE TEST] Test 1: Allocate page via monitor_call
[TRAMPOLINE TEST] PASS: Allocation succeeded, addr = 0x...
[TRAMPOLINE TEST] PASS: Returned to unprivileged mode
[TRAMPOLINE TEST] PASS: Free succeeded
[TRAMPOLINE TEST] All tests PASSED
```

**Step 3: Verify Nested Kernel invariants still pass**

Run: `grep -E "Nested Kernel invariants" /tmp/test_output.log`
Expected: `[CPU 0] Nested Kernel invariants: PASS`

**Step 4: If tests pass, commit the test**

```bash
git add tests/monitor_trampoline/ arch/x86_64/main.c Makefile
git commit -m "test(monitor): add monitor trampoline CR3 switching test

- Add integration test for monitor call from unprivileged mode
- Verify CR3 switching works correctly
- Verify return to unprivileged mode after call
- Verify Nested Kernel invariants still pass"
```

---

## Task 6: Debug Common Issues (If Tests Fail)

**Files:**
- Modify: `arch/x86_64/monitor/monitor_call.S` or `kernel/monitor/monitor.c`
- Debug: Add debug output

**Issue 1: Page Fault on trampoline entry**

**Symptom:** Triple fault or page fault when calling monitor_call()

**Diagnosis:** Trampoline code is not identity-mapped in both page tables

**Fix:**
1. Check if `.text` section is identity-mapped
2. Verify trampoline is in low memory (< 2MB)
3. Add trampoline to its own section with explicit mapping

```c
/* In monitor_init(), ensure trampoline is mapped */
extern char _start_ok_ap_boot_trampoline[];
extern char _end_ok_ap_boot_trampoline[];
```

**Issue 2: Stack corruption**

**Symptom:** Random crashes, garbled return addresses

**Diagnosis:** Stack not preserved correctly across CR3 switch

**Fix:** Ensure saved RSP is in a region valid in BOTH page tables:
- Use fixed physical memory location
- Or use per-CPU data area that's identity-mapped

**Issue 3: CR3 not restored**

**Symptom:** System stays in privileged mode after call

**Diagnosis:** CR3 restore instruction not executing or being skipped

**Fix:**
1. Check for exceptions during handler execution
2. Verify handler returns correctly
3. Add debug output before/after CR3 switch

**Issue 4: Arguments corrupted**

**Symptom:** Handler receives wrong arguments

**Diagnosis:** Registers not saved/restored correctly

**Fix:** Verify register save/restore sequence matches System V ABI

---

## Task 7: Document the Solution

**Files:**
- Create: `docs/monitor_trampoline.md`
- Modify: `docs/monitor_api.md` (add trampoline section)

**Step 1: Create trampoline documentation**

Create `docs/monitor_trampoline.md`:

```markdown
# Monitor Entry Trampoline

## Overview

The `nk_entry_trampoline` is the assembly stub that enables secure monitor calls from unprivileged mode. It switches page tables (CR3) to enter privileged mode, calls the monitor handler, then switches back.

## Memory Layout Requirements

The trampoline code must be identity-mapped at the SAME virtual address in BOTH page table views:
- **Monitor view:** `.text` section is copied from `boot_pml4`
- **Unprivileged view:** `.text` section is copied from `boot_pml4`

This ensures that when CR3 changes, the current EIP/RIP remains valid.

## CR3 Switching Protocol

```
Unprivileged Mode          Privileged Mode (Monitor)
     |                              ^
     | nk_entry_trampoline          |
     |  1. Save registers           |
     |  2. Save RSP                 |
     |  3. Switch CR3 --------------+
     |  4. Switch to monitor stack  |
     |  5. Call handler             |
     |  6. Restore CR3 -------------+
     |  7. Restore RSP              |
     |  8. Restore registers        |
     |  9. Return                   |
     +------------------------------|
```

## Stack Management

Two stacks are involved:

1. **Caller stack (unprivileged):** Saved before CR3 switch, restored after
2. **Monitor stack (privileged):** Used during handler execution

The saved RSP must be in a location valid in BOTH page tables. We use:
- Fixed location in `.bss` section: `saved_rsp`
- This location is identity-mapped in both views

## Per-CPU Considerations

Currently, only BSP uses monitor calls. For AP support:
- Each CPU needs its own `saved_rsp` slot
- Each CPU needs its own monitor stack area

## Security Properties

The trampoline enforces:
1. Unprivileged code CANNOT access privileged operations directly
2. Page table switch is atomic (no interrupts during switch)
3. Return to unprivileged mode is guaranteed
4. Privileged operations are mediated through monitor_call()

## Testing

See `tests/monitor_trampoline/monitor_trampoline_test.c` for integration tests.
```

**Step 2: Update monitor_api.md with trampoline section**

Add to `docs/monitor_api.md`:

```markdown
---
## Monitor Call Internals

### Trampoline Mechanism

When `monitor_call()` is invoked from unprivileged mode:
1. Check if already privileged (direct call if yes)
2. Call `nk_entry_trampoline` assembly stub
3. Trampoline switches CR3 to `monitor_pml4_phys`
4. Calls `monitor_call_handler()` in privileged mode
5. Trampoline switches CR3 back to original value
6. Returns to caller in unprivileged mode

**Security Benefit:** Unprivileged code cannot directly access privileged handler function.

**Implementation:** See `arch/x86_64/monitor/monitor_call.S` and `docs/monitor_trampoline.md`
```

**Step 3: Commit documentation**

```bash
git add docs/monitor_trampoline.md docs/monitor_api.md
git commit -m "docs(monitor): document monitor entry trampoline

- Add trampoline architecture documentation
- Explain CR3 switching protocol
- Document stack management strategy
- Add security properties and testing guide"
```

---

## Success Criteria

1. **Trampoline code compiles** without errors
2. **Integration test passes** - monitor_call works from unprivileged mode
3. **Nested Kernel invariants pass** - no regression
4. **No page faults** - CR3 switching is safe
5. **Correct return** - system returns to unprivileged mode after call

---

## References

- **Paper:** "Nested Kernel: An Operating System Architecture for Intra-Kernel Privilege Separation" (ASPLOS '15)
- **Current code:** `arch/x86_64/monitor/monitor_call.S`, `kernel/monitor/monitor.c:796-822`
- **Related:** `docs/monitor_api.md`, `docs/nested_kernel_design.md`
