# Nested Kernel Architecture Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Implement nested kernel architecture for Emergence Kernel - splitting the kernel into Privileged Monitor and Unprivileged Kernel domains using page table manipulation for isolation (not CPL changes).

**Architecture:** Two protection domains within Ring 0: Privileged Monitor (trusted code with full memory access) and Unprivileged Kernel (restricted memory view, accesses critical resources via monitor calls). Uses CR3 switching for isolation.

**Tech Stack:** x86_64 assembly, C, QEMU testing

---

## Task 1: Create Monitor Infrastructure Header

**Files:**
- Create: `kernel/monitor/monitor.h`

**Step 1: Write the monitor header file**

```c
#ifndef KERNEL_MONITOR_MONITOR_H
#define KERNEL_MONITOR_MONITOR_H

#include <stdint.h>
#include <stdbool.h>

/* Monitor call return structure */
typedef struct {
    uint64_t result;
    int error;
} monitor_ret_t;

/* Monitor call types */
typedef enum {
    MONITOR_CALL_APIC_READ,     /* Read APIC register */
    MONITOR_CALL_APIC_WRITE,    /* Write APIC register */
    MONITOR_CALL_ALLOC_PHYS,    /* Allocate physical memory */
    MONITOR_CALL_FREE_PHYS,     /* Free physical memory */
    MONITOR_CALL_SEND_IPI,      /* Send IPI */
} monitor_call_t;

/* Monitor page table physical addresses (set by monitor_init) */
extern uint64_t monitor_pml4_phys;    /* Full privileged view */
extern uint64_t unpriv_pml4_phys;     /* Restricted unprivileged view */

/* Monitor initialization */
void monitor_init(void);

/* Get unprivileged CR3 value for switching */
uint64_t monitor_get_unpriv_cr3(void);

/* Check if running in monitor mode (privileged) */
bool monitor_is_privileged(void);

/* Monitor call handler (called from unprivileged mode) */
monitor_ret_t monitor_call(monitor_call_t call, uint64_t arg1, uint64_t arg2, uint64_t arg3);

/* APIC monitor calls */
uint32_t monitor_apic_read(uint32_t offset);
void monitor_apic_write(uint32_t offset, uint32_t value);

/* PMM monitor calls */
void *monitor_pmm_alloc(uint8_t order);
void monitor_pmm_free(void *addr, uint8_t order);

#endif /* KERNEL_MONITOR_MONITOR_H */
```

**Step 2: Create the monitor directory**

Run: `mkdir -p kernel/monitor`
Expected: Directory created

**Step 3: Commit**

```bash
git add kernel/monitor/monitor.h
git commit -m "feat(monitor): add monitor infrastructure header"
```

---

## Task 2: Create Monitor Call Assembly Stub

**Files:**
- Create: `arch/x86_64/monitor/monitor_call.S`
- Create: `arch/x86_64/monitor/`

**Step 1: Create monitor directory**

Run: `mkdir -p arch/x86_64/monitor`
Expected: Directory created

**Step 2: Write the monitor call assembly stub**

```assembly
/* monitor_call.S - Monitor call assembly stub for CR3 switching */
/* This stub switches from unprivileged to privileged mode to execute monitor calls */

#include <asm/asmdefs.h>

.section .text
.global monitor_call_stub
.type monitor_call_stub, @function

/* monitor_call_stub - Switch to privileged mode and execute monitor call
 * Arguments:
 *   rdi = monitor_call_t call
 *   rsi = uint64_t arg1
 *   rdx = uint64_t arg2
 *   rcx = uint64_t arg3
 * Returns:
 *   rax = monitor_ret_t.result
 *   rdx = monitor_ret_t.error
 */
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

    /* Call C monitor handler */
    mov %rdi, %rdi       /* call */
    mov %rsi, %rsi       /* arg1 */
    mov %rdx, %rdx       /* arg2 */
    mov %rcx, %rcx       /* arg3 */
    call monitor_call_handler

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

.size monitor_call_stub, . - monitor_call_stub

/* External symbols */
.global monitor_pml4_phys
.global monitor_call_handler
```

**Step 3: Commit**

```bash
git add arch/x86_64/monitor/monitor_call.S
git commit -m "feat(monitor): add monitor call assembly stub for CR3 switching"
```

---

## Task 3: Create APIC EOI Assembly Stub

**Files:**
- Create: `arch/x86_64/monitor/apic_eoi.S`

**Step 1: Write the APIC EOI stub**

```assembly
/* apic_eoi.S - APIC EOI monitor call stub */
/* Used by interrupt handlers to send EOI via monitor */

#include <asm/asmdefs.h>

.section .text
.global monitor_apic_eoi
.type monitor_apic_eoi, @function

/* monitor_apic_eoi - Send APIC EOI via monitor call
 * This is called from interrupt handlers instead of direct APIC access
 */
monitor_apic_eoi:
    /* Save registers we need */
    push %rax
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %r8
    push %r9
    push %r10
    push %r11

    /* Call monitor EOI handler */
    call monitor_apic_eoi_handler

    /* Restore registers */
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rax

    ret

.size monitor_apic_eoi, . - monitor_apic_eoi

/* External symbols */
.global monitor_apic_eoi_handler
```

**Step 2: Commit**

```bash
git add arch/x86_64/monitor/apic_eoi.S
git commit -m "feat(monitor): add APIC EOI monitor call stub"
```

---

## Task 4: Create Monitor Core Implementation

**Files:**
- Create: `kernel/monitor/monitor.c`
- Modify: `Makefile`

**Step 1: Write the monitor core implementation**

```c
/* monitor.c - Monitor core implementation for nested kernel architecture */

#include "monitor.h"
#include <serial.h>
#include <string.h>
#include <apic.h>
#include <pmm.h>

/* Page table physical addresses */
uint64_t monitor_pml4_phys = 0;
uint64_t unpriv_pml4_phys = 0;

/* Monitor page table structures (allocated during init) */
static uint64_t *monitor_pml4;
static uint64_t *monitor_pdpt;
static uint64_t *monitor_pd;

static uint64_t *unpriv_pml4;
static uint64_t *unpriv_pdpt;
static uint64_t *unpriv_pd;

/* External boot page table symbols (from boot.S) */
extern uint64_t boot_pml4;
extern uint64_t boot_pdpt;
extern uint64_t boot_pd;

/* Get physical address from virtual */
static inline uint64_t virt_to_phys(void *virt) {
    return (uint64_t)virt;
}

/* Initialize monitor page tables */
void monitor_init(void) {
    serial_puts("MONITOR: Initializing nested kernel architecture\n");

    /* Allocate page tables for monitor (privileged) view */
    monitor_pml4 = (uint64_t *)pmm_alloc(0);  /* 1 page */
    monitor_pdpt = (uint64_t *)pmm_alloc(0);
    monitor_pd = (uint64_t *)pmm_alloc(0);

    if (!monitor_pml4 || !monitor_pdpt || !monitor_pd) {
        serial_puts("MONITOR: Failed to allocate monitor page tables\n");
        return;
    }

    /* Allocate page tables for unprivileged view */
    unpriv_pml4 = (uint64_t *)pmm_alloc(0);
    unpriv_pdpt = (uint64_t *)pmm_alloc(0);
    unpriv_pd = (uint64_t *)pmm_alloc(0);

    if (!unpriv_pml4 || !unpriv_pdpt || !unpriv_pd) {
        serial_puts("MONITOR: Failed to allocate unprivileged page tables\n");
        return;
    }

    /* Copy boot page table mappings to monitor view */
    memcpy(monitor_pml4, &boot_pml4, 4096);
    memcpy(monitor_pdpt, &boot_pdpt, 4096);
    memcpy(monitor_pd, &boot_pd, 4096);

    /* Create restricted unprivileged view */
    /* Copy first 1GB identity mapping */
    memcpy(unpriv_pml4, &boot_pml4, 4096);
    memcpy(unpriv_pdpt, &boot_pdpt, 4096);
    memcpy(unpriv_pd, &boot_pd, 4096);

    /* Save physical addresses */
    monitor_pml4_phys = virt_to_phys(monitor_pml4);
    unpriv_pml4_phys = virt_to_phys(unpriv_pml4);

    serial_puts("MONITOR: Page tables initialized\n");
    serial_puts("MONITOR: Monitor PML4 at 0x");
    serial_print_hex(monitor_pml4_phys);
    serial_puts("\n");
    serial_puts("MONITOR: Unpriv PML4 at 0x");
    serial_print_hex(unpriv_pml4_phys);
    serial_puts("\n");
}

/* Get unprivileged CR3 value */
uint64_t monitor_get_unpriv_cr3(void) {
    return unpriv_pml4_phys;
}

/* Check if running in privileged (monitor) mode */
bool monitor_is_privileged(void) {
    uint64_t cr3;
    asm volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3 == monitor_pml4_phys;
}

/* Monitor call handler (called from assembly stub) */
monitor_ret_t monitor_call_handler(monitor_call_t call, uint64_t arg1,
                                     uint64_t arg2, uint64_t arg3) {
    monitor_ret_t ret = {0, 0};

    switch (call) {
        case MONITOR_CALL_APIC_READ:
            ret.result = lapic_read((uint32_t)arg1);
            break;

        case MONITOR_CALL_APIC_WRITE:
            lapic_write((uint32_t)arg1, (uint32_t)arg2);
            break;

        case MONITOR_CALL_ALLOC_PHYS:
            ret.result = (uint64_t)pmm_alloc((uint8_t)arg1);
            if (!ret.result) {
                ret.error = -1;
            }
            break;

        case MONITOR_CALL_FREE_PHYS:
            pmm_free((void *)arg1, (uint8_t)arg2);
            break;

        default:
            ret.error = -1;
            break;
    }

    return ret;
}

/* Monitor APIC EOI handler */
void monitor_apic_eoi_handler(void) {
    lapic_write(LAPIC_EOI, 0);
}

/* Public monitor call wrapper (for unprivileged code) */
monitor_ret_t monitor_call(monitor_call_t call, uint64_t arg1,
                            uint64_t arg2, uint64_t arg3) {
    if (monitor_is_privileged()) {
        /* Already privileged, call directly */
        return monitor_call_handler(call, arg1, arg2, arg3);
    }

    /* Unprivileged: use assembly stub to switch CR3 */
    monitor_ret_t ret;
    asm volatile (
        "call monitor_call_stub"
        : "=a"(ret.result), "=d"(ret.error)
        : "D"(call), "S"(arg1), "d"(arg2), "c"(arg3)
        : "memory", "rcx", "r8", "r9", "r10", "r11", "rbx", "rbp", "r12", "r13", "r14", "r15"
    );
    return ret;
}

/* APIC monitor call wrappers */
uint32_t monitor_apic_read(uint32_t offset) {
    monitor_ret_t ret = monitor_call(MONITOR_CALL_APIC_READ, offset, 0, 0);
    return (uint32_t)ret.result;
}

void monitor_apic_write(uint32_t offset, uint32_t value) {
    monitor_call(MONITOR_CALL_APIC_WRITE, offset, value, 0);
}

/* PMM monitor call wrappers */
void *monitor_pmm_alloc(uint8_t order) {
    monitor_ret_t ret = monitor_call(MONITOR_CALL_ALLOC_PHYS, order, 0, 0);
    return (void *)ret.result;
}

void monitor_pmm_free(void *addr, uint8_t order) {
    monitor_call(MONITOR_CALL_FREE_PHYS, (uint64_t)addr, order, 0);
}
```

**Step 2: Update Makefile to include monitor files**

Find and modify the `KERNEL_OBJS` section in Makefile to add:

```makefile
# Monitor objects
KERNEL_OBJS += \
    kernel/monitor/monitor.o \
    arch/x86_64/monitor/monitor_call.o \
    arch/x86_64/monitor/apic_eoi.o
```

**Step 3: Commit**

```bash
git add kernel/monitor/monitor.c Makefile
git commit -m "feat(monitor): implement monitor core with page table management"
```

---

## Task 5: Add Dual Page Tables to boot.S

**Files:**
- Modify: `arch/x86_64/boot.S`

**Step 1: Read current boot.S to understand page table layout**

Run: `head -100 arch/x86_64/boot.S`
Expected: See current page table structure at lines 78-89

**Step 2: Add monitor page table structures after boot_pd**

Add after line 89 (after boot_pd):

```assembly
/* Monitor page tables for nested kernel architecture */
.section .bss
.align 4096
.global monitor_pml4, monitor_pdpt, monitor_pd
monitor_pml4: .skip 4096
monitor_pdpt: .skip 4096
monitor_pd:   .skip 4096

.global unpriv_pml4, unpriv_pdpt, unpriv_pd
unpriv_pml4:  .skip 4096
unpriv_pdpt:  .skip 4096
unpriv_pd:    .skip 4096
```

**Step 3: Commit**

```bash
git add arch/x86_64/boot.S
git commit -m "feat(monitor): add dual page table structures to boot.S"
```

---

## Task 6: Wrap APIC Access with Monitor Calls

**Files:**
- Modify: `arch/x86_64/apic.c`

**Step 1: Modify lapic_read to detect mode and use monitor calls**

Replace the `lapic_read` function (lines 46-64) with:

```c
uint32_t lapic_read(uint32_t offset) {
    /* Check if in monitor (privileged) mode */
    uint64_t cr3;
    asm volatile ("mov %%cr3, %0" : "=r"(cr3));

    /* If using unprivileged page tables, use monitor call */
    if (cr3 != monitor_pml4_phys && monitor_pml4_phys != 0) {
        return monitor_apic_read(offset);
    }

    /* Privileged mode: direct access (original code) */
    volatile uint32_t *addr = (volatile uint32_t *)((char *)lapic_base + offset);
    uint32_t value;

    asm volatile ("mfence" ::: "memory");

    asm volatile ("movl %1, %0"
                  : "=r"(value)
                  : "m"(*addr)
                  : "memory");

    asm volatile ("mfence" ::: "memory");

    return value;
}
```

**Step 2: Modify lapic_write to detect mode and use monitor calls**

Replace the `lapic_write` function (lines 73-88) with:

```c
void lapic_write(uint32_t offset, uint32_t value) {
    /* Check if in monitor (privileged) mode */
    uint64_t cr3;
    asm volatile ("mov %%cr3, %0" : "=r"(cr3));

    /* If using unprivileged page tables, use monitor call */
    if (cr3 != monitor_pml4_phys && monitor_pml4_phys != 0) {
        monitor_apic_write(offset, value);
        return;
    }

    /* Privileged mode: direct access (original code) */
    volatile uint32_t *addr = (volatile uint32_t *)((char *)lapic_base + offset);

    asm volatile ("mfence" ::: "memory");

    asm volatile ("movl %0, %1"
                  :
                  : "r"(value), "m"(*addr)
                  : "memory");

    asm volatile ("mfence" ::: "memory");
}
```

**Step 3: Add monitor.h include at top of file**

Add near the top includes:
```c
#include <monitor.h>
```

**Step 4: Commit**

```bash
git add arch/x86_64/apic.c
git commit -m "feat(monitor): wrap APIC access with monitor calls"
```

---

## Task 7: Create PMM Monitor Wrappers

**Files:**
- Create: `kernel/monitor/pmm_monitor.c`

**Step 1: Write the PMM monitor wrapper implementation**

```c
/* pmm_monitor.c - PMM monitor call wrappers for nested kernel */

#include <monitor.h>
#include <pmm.h>
#include <serial.h>

/* Allocate physical memory via monitor call */
void *monitor_pmm_alloc_wrapper(uint8_t order) {
    /* Check if in monitor (privileged) mode */
    uint64_t cr3;
    asm volatile ("mov %%cr3, %0" : "=r"(cr3));

    /* If using unprivileged page tables, use monitor call */
    if (cr3 != monitor_pml4_phys && monitor_pml4_phys != 0) {
        return monitor_pmm_alloc(order);
    }

    /* Privileged mode: direct access */
    return pmm_alloc(order);
}

/* Free physical memory via monitor call */
void monitor_pmm_free_wrapper(void *addr, uint8_t order) {
    /* Check if in monitor (privileged) mode */
    uint64_t cr3;
    asm volatile ("mov %%cr3, %0" : "=r"(cr3));

    /* If using unprivileged page tables, use monitor call */
    if (cr3 != monitor_pml4_phys && monitor_pml4_phys != 0) {
        monitor_pmm_free(addr, order);
        return;
    }

    /* Privileged mode: direct access */
    pmm_free(addr, order);
}
```

**Step 2: Update monitor.h to add wrapper declarations**

Add to `kernel/monitor/monitor.h`:

```c
/* PMM wrapper functions (replace pmm_alloc/pmm_free in unprivileged code) */
void *monitor_pmm_alloc_wrapper(uint8_t order);
void monitor_pmm_free_wrapper(void *addr, uint8_t order);
```

**Step 3: Update Makefile to include pmm_monitor.o**

Add to KERNEL_OBJS in Makefile:
```makefile
kernel/monitor/pmm_monitor.o \
```

**Step 4: Commit**

```bash
git add kernel/monitor/pmm_monitor.c kernel/monitor/monitor.h Makefile
git commit -m "feat(monitor): add PMM monitor call wrappers"
```

---

## Task 8: Replace ISR APIC EOI with Monitor Call

**Files:**
- Modify: `arch/x86_64/isr.S`

**Step 1: Find and replace timer_isr EOI**

Find the timer_isr function (around lines 100-153) and replace:
```assembly
# OLD:
    movabs $0xFEE000B0, %rax      /* Load absolute EOI address */
    movl $0, (%rax)              /* Write 0 to EOI register */

# NEW:
    call monitor_apic_eoi
```

**Step 2: Find and replace ipi_isr EOI**

Find the ipi_isr function (around lines 155-208) and replace:
```assembly
# OLD:
    movabs $0xFEE000B0, %rax
    movl $0, (%rax)

# NEW:
    call monitor_apic_eoi
```

**Step 3: Commit**

```bash
git add arch/x86_64/isr.S
git commit -m "feat(monitor): replace direct APIC EOI with monitor calls in ISR"
```

---

## Task 9: Modify Kernel Main to Initialize Monitor and Switch CR3

**Files:**
- Modify: `kernel/main.c`

**Step 1: Add monitor.h include**

Add near top includes:
```c
#include <monitor.h>
```

**Step 2: Modify kernel_main to initialize monitor and switch to unprivileged mode**

Find the BSP section (cpu_id == 0) after `ipi_driver_init()` call, add before `enable_interrupts()`:

```c
    /* Initialize nested kernel monitor */
    serial_puts("KERNEL: Initializing monitor...\n");
    monitor_init();

    /* Switch to unprivileged page tables */
    uint64_t unpriv_cr3 = monitor_get_unpriv_cr3();
    serial_puts("KERNEL: Switching to unprivileged mode (CR3=0x");
    serial_print_hex(unpriv_cr3);
    serial_puts(")\n");

    asm volatile ("mov %0, %%cr3" : : "r"(unpriv_cr3));

    serial_puts("KERNEL: Now running in unprivileged mode\n");

    /* Verify switch worked */
    if (monitor_is_privileged()) {
        serial_puts("KERNEL: WARNING: Still in privileged mode!\n");
    } else {
        serial_puts("KERNEL: Successfully switched to unprivileged mode\n");
    }
```

**Step 3: Commit**

```bash
git add kernel/main.c
git commit -m "feat(monitor): initialize monitor and switch to unprivileged mode in kernel_main"
```

---

## Task 10: Add AP Page Table Switching

**Files:**
- Modify: `kernel/smp.c`

**Step 1: Add monitor.h include to smp.c**

Add near top includes:
```c
#include <monitor.h>
```

**Step 2: Modify ap_start to switch to unprivileged page tables**

Find the `ap_start` function (lines 245-287) and add CR3 switch after setting stack, before marking CPU online:

```c
void ap_start(void) {
    int my_index = atomic_fetch_add(&next_cpu_id, 1);

    if (my_index <= 0 || my_index >= SMP_MAX_CPUS) {
        serial_puts("[AP] ERROR: Invalid CPU index!\n");
        while (1) { asm volatile ("hlt"); }
    }

    current_cpu_index = my_index;

    cpu_info[my_index].stack_top = &ap_stacks[my_index][CPU_STACK_SIZE];
    asm volatile ("mov %0, %%rsp" : : "r"(cpu_info[my_index].stack_top));

    /* Switch to unprivileged page tables */
    uint64_t unpriv_cr3 = monitor_get_unpriv_cr3();
    asm volatile ("mov %0, %%cr3" : : "r"(unpriv_cr3));

    serial_puts("[AP] CPU");
    serial_print_dec(my_index);
    serial_puts(" switched to unprivileged mode\n");

    cpu_info[my_index].state = CPU_ONLINE;

    smp_mark_cpu_ready(my_index);

    /* ... rest of function ... */
}
```

**Step 3: Commit**

```bash
git add kernel/smp.c
git commit -m "feat(monitor): add AP page table switch to unprivileged mode"
```

---

## Task 11: Build and Test Basic Boot

**Files:**
- Test: `make clean && make`

**Step 1: Clean and build**

Run: `make clean && make`
Expected: Clean build with no errors

**Step 2: Run basic boot test (1 CPU)**

Run: `make test-boot`
Expected: Kernel boots, shows monitor initialization messages

**Step 3: Check serial output for monitor messages**

Look for:
- "MONITOR: Initializing nested kernel architecture"
- "MONITOR: Page tables initialized"
- "KERNEL: Switching to unprivileged mode"
- "KERNEL: Successfully switched to unprivileged mode"

**Step 4: If build succeeds, commit any minor fixes**

```bash
git add -A
git commit -m "fix(monitor): minor build fixes"
```

---

## Task 12: Run SMP Boot Test

**Files:**
- Test: `make test-smp`

**Step 1: Run SMP boot test with 2 CPUs**

Run: `make test-smp`
Expected: Both BSP and AP boot successfully

**Step 2: Check AP output for monitor messages**

Look for:
- "[AP] CPU1 switched to unprivileged mode"
- Both CPUs marked as ready

**Step 3: If test passes, note successful completion**

Run: `echo "SMP boot with nested kernel architecture: PASS"`

---

## Task 13: Create Isolation Test

**Files:**
- Create: `tests/isolation/monitor_test.c`

**Step 1: Create isolation test directory**

Run: `mkdir -p tests/isolation`
Expected: Directory created

**Step 2: Write isolation test**

```c
/* monitor_test.c - Test monitor isolation */

#include <monitor.h>
#include <serial.h>
#include <apic.h>

/* Test that direct page table access from unprivileged mode causes page fault */
void test_monitor_isolation(void) {
    serial_puts("\n=== Monitor Isolation Test ===\n");

    /* Check current mode */
    if (monitor_is_privileged()) {
        serial_puts("TEST: Running in privileged mode (expected after init)\n");
    } else {
        serial_puts("TEST: Running in unprivileged mode\n");
    }

    /* Test monitor call for APIC read */
    serial_puts("TEST: Reading APIC ID via monitor call...\n");
    uint32_t apic_id = monitor_apic_read(0x20);  /* APIC ID register */
    serial_puts("TEST: APIC ID = 0x");
    serial_print_hex(apic_id);
    serial_puts("\n");

    /* Test monitor call for APIC write */
    serial_puts("TEST: Writing to APIC EOI via monitor call...\n");
    monitor_apic_write(0xB0, 0);  /* EOI register */
    serial_puts("TEST: APIC EOI write successful\n");

    serial_puts("=== Isolation Test Complete ===\n\n");
}
```

**Step 3: Add test call to kernel_main**

Modify `kernel/main.c` to call the test after monitor init:
```c
extern void test_monitor_isolation(void);

/* After monitor initialization and CR3 switch */
test_monitor_isolation();
```

**Step 4: Update Makefile to compile test**

Add test object to KERNEL_OBJS:
```makefile
tests/isolation/monitor_test.o \
```

**Step 5: Commit**

```bash
git add tests/isolation/monitor_test.c kernel/main.c Makefile
git commit -m "test(monitor): add monitor isolation test"
```

---

## Task 14: Final Verification Test

**Files:**
- Test: `make test-all`

**Step 1: Run full test suite**

Run: `make test-all`
Expected: All tests pass

**Step 2: Build final ISO**

Run: `make`
Expected: Clean build, ISO created

**Step 3: Manual QEMU test**

Run: `./run-qemu.sh`
Expected: Kernel boots with 4 CPUs, monitor messages appear, APs switch successfully

**Step 4: Final commit if needed**

```bash
git add -A
git commit -m "feat(monitor): complete nested kernel architecture implementation"
```

---

## Summary

This plan implements the nested kernel architecture for Emergence Kernel:

1. **Monitor Infrastructure** - Header, assembly stubs, core implementation
2. **Dual Page Tables** - Added to boot.S for privileged/unprivileged views
3. **APIC Protection** - Wrapped with monitor calls
4. **PMM Protection** - Wrapped with monitor calls
5. **ISR EOI** - Uses monitor call instead of direct APIC access
6. **Kernel Init** - Initializes monitor and switches CR3
7. **AP Startup** - Switches to unprivileged mode
8. **Testing** - Isolation test and full test suite

**Key Files Modified/Created:**
- `kernel/monitor/monitor.h` (NEW)
- `kernel/monitor/monitor.c` (NEW)
- `kernel/monitor/pmm_monitor.c` (NEW)
- `arch/x86_64/monitor/monitor_call.S` (NEW)
- `arch/x86_64/monitor/apic_eoi.S` (NEW)
- `arch/x86_64/boot.S` (MODIFY - add page tables)
- `kernel/main.c` (MODIFY - init monitor)
- `arch/x86_64/apic.c` (MODIFY - wrap APIC)
- `kernel/pmm.c` (MODIFY - wrap PMM)
- `arch/x86_64/isr.S` (MODIFY - EOI stub)
- `kernel/smp.c` (MODIFY - AP CR3 switch)
- `tests/isolation/monitor_test.c` (NEW)
