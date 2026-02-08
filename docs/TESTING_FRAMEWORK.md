# Emergence Kernel - Testing Framework Design

## Table of Contents

1. [Overview](#overview)
2. [Current Design (Pre-Refactoring)](#current-design-pre-refactoring)
3. [New Design (Post-Refactoring)](#new-design-post-refactoring)
4. [Adding a New Test Case](#adding-a-new-test-case)
5. [Deleting a Test Case](#deleting-a-test-case)
6. [Running Test Cases](#running-test-cases)
7. [Checking Test Results](#checking-test-results)
8. [Migration Guide](#migration-guide)

---

## Overview

The Emergence Kernel testing framework provides a structured way to test kernel functionality at boot time. Tests are compiled directly into the kernel binary and execute during the boot sequence.

### Key Principles

1. **Compile-time inclusion**: Tests are included/excluded via `CONFIG_*_TESTS` flags in `kernel.config`
2. **Fail-fast behavior**: Test failures trigger immediate system shutdown
3. **Serial output**: All test results are written to serial console for capture
4. **Subsystem-specific timing**: Tests run after their dependent subsystems initialize

---

## Current Design (Pre-Refactoring)

### Architecture

```
arch/x86_64/main.c
├── Preprocessor directives (#if CONFIG_*_TESTS)
├── Inline test code blocks
└── Hardcoded test execution order
```

### Current Test Locations

| Test | Location | Run Point | Trigger |
|------|----------|-----------|---------|
| PMM Tests | Inline in `main.c:110-150` | After `pmm_init()` | Always (if compiled) |
| Slab Tests | `tests/slab/slab_test.c` | After `slab_init()` | Always (if compiled) |
| Spinlock Tests | `tests/spinlock/spinlock_test.c` | After AP startup | Always (if compiled) |
| NK Protection Tests | `tests/nk_protection/nk_protection_test.c` | BSP shutdown | Always (if compiled) |

### Current Execution Flow

```c
void kernel_main(uint32_t multiboot_info_addr) {
    // ... boot sequence ...

#if CONFIG_PMM_TESTS
    // Inline PMM test code (lines 110-150)
    serial_puts("[ PMM tests ] Running allocation tests...\n");
    // ... test code ...
#endif

#if CONFIG_SLAB_TESTS
    extern int run_slab_tests(void);
    int slab_failures = run_slab_tests();
    // ... report results ...
#endif

#if CONFIG_SPINLOCK_TESTS
    extern int run_spinlock_tests(void);
    int test_failures = run_spinlock_tests();
    // ... report results ...
#endif

#if CONFIG_NK_PROTECTION_TESTS
    extern int run_nk_protection_tests(void);
    run_nk_protection_tests();  // Never returns
#endif

    system_shutdown();
}
```

### Current Limitations

1. **No runtime selection**: All compiled tests run automatically
2. **Requires recompilation**: To change which tests run, rebuild the kernel
3. **No unified execution point**: Tests scattered throughout init sequence
4. **No manual-only tests**: All tests run if compiled in
5. **Inline PMM tests**: PMM tests are embedded in main.c instead of separate file

---

## New Design (Post-Refactoring)

### Architecture

```
kernel/test.c (Test Framework)
├── test_registry[] - Array of test_case_t
├── test_framework_init() - Parse cmdline
├── test_should_run() - Check if test selected
├── test_run_by_name() - Execute test with fail-fast
└── test_run_unified() - Run all selected at once
```

### Test Registry Structure

```c
typedef struct test_case {
    const char *name;           /* Test name (e.g., "spinlock", "slab") */
    const char *description;    /* Human-readable description */
    int (*run_func)(void);      /* Test function pointer */
    int enabled;                /* Compile-time enable flag (CONFIG_TEST_*) */
    int auto_run;               /* Auto-run after subsystem init (1=auto, 0=manual) */
} test_case_t;

const test_case_t test_registry[] = {
#if CONFIG_SPINLOCK_TESTS
    { .name = "spinlock",
      .description = "Spin lock and read-write lock synchronization tests",
      .run_func = run_spinlock_tests,
      .enabled = 1,
      .auto_run = 1 },  /* Auto-run after AP startup */
#endif
#if CONFIG_SLAB_TESTS
    { .name = "slab",
      .description = "Slab allocator small object allocation tests",
      .run_func = run_slab_tests,
      .enabled = 1,
      .auto_run = 1 },  /* Auto-run after slab_init() */
#endif
#if CONFIG_PMM_TESTS
    { .name = "pmm",
      .description = "Physical memory manager allocation tests",
      .run_func = run_pmm_tests,
      .enabled = 1,
      .auto_run = 1 },  /* Auto-run after pmm_init() */
#endif
#if CONFIG_NK_PROTECTION_TESTS
    { .name = "nk_protection",
      .description = "Nested kernel mappings protection tests (destructive)",
      .run_func = run_nk_protection_tests,
      .enabled = 1,
      .auto_run = 0 },  /* Manual only (destructive test) */
#endif
    { .name = NULL }  // Sentinel
};
```

### Command Line Interface

```bash
# No test= parameter: NO tests run (default behavior)
make run KERNEL_CMDLINE="quote=\"Hello\""

# test=all: Run all enabled tests (distributed auto-run)
make run KERNEL_CMDLINE="test=all"

# test=<name>: Run specific test
make run KERNEL_CMDLINE="test=spinlock"

# test=unified: Run all selected tests at unified point
make run KERNEL_CMDLINE="test=unified"
```

### Execution Modes

#### Mode 1: Distributed Auto-Run (Default with `test=all`)

Tests execute at their subsystem init points when selected:

```c
void kernel_main(uint32_t multiboot_info_addr) {
    // ...
    test_framework_init();           // Parse test= parameter

    pmm_init();
#if CONFIG_PMM_TESTS
    if (test_should_run("pmm")) {
        test_run_by_name("pmm");     // Fail-fast: shutdown on error
    }
#endif

    slab_init();
#if CONFIG_SLAB_TESTS
    if (test_should_run("slab")) {
        test_run_by_name("slab");
    }
#endif

    smp_start_all_aps();
#if CONFIG_SPINLOCK_TESTS
    if (test_should_run("spinlock")) {
        test_run_by_name("spinlock");
    }
#endif

#if CONFIG_NK_PROTECTION_TESTS
    if (test_should_run("nk_protection")) {
        test_run_by_name("nk_protection");  // Manual-only test
    }
#endif

    test_run_unified();              // If test=unified, run pending
    system_shutdown();
}
```

#### Mode 2: Unified Manual Execution

With `test=unified`, all selected tests run at end of init:

```c
test_run_unified() {
    for each test in registry:
        if test selected and not yet run:
            run test
            if failure: system_shutdown()
}
```

### Key Framework Functions

```c
/* In kernel/test.c */

/**
 * test_framework_init() - Parse test= parameter from kernel cmdline
 *
 * Reads test=<name|all|unified> from cmdline and stores selection.
 * Must be called once after multiboot_get_cmdline().
 */
void test_framework_init(void);

/**
 * test_should_run() - Check if test should run in auto mode
 * @name: Test name (e.g., "spinlock", "slab")
 *
 * Returns: 1 if test should run, 0 otherwise
 *
 * Returns 1 if:
 * - test=all is set AND test is enabled and auto_run=1
 * - test=<name> matches this test name
 * Returns 0 if no test= parameter or test not selected
 */
int test_should_run(const char *name);

/**
 * test_run_by_name() - Execute test by name with fail-fast
 * @name: Test name
 *
 * Executes the test and calls system_shutdown() on failure.
 *
 * Returns: 0 on success, non-zero on failure (but shutdown occurs first)
 */
int test_run_by_name(const char *name);

/**
 * test_run_unified() - Run all selected tests at unified point
 *
 * Executes all selected tests that haven't run yet.
 * Called at end of BSP init when test=unified is specified.
 *
 * Returns: 0 on success, non-zero if any test failed
 */
int test_run_unified(void);
```

### Fail-Fast Behavior

```c
int test_run_by_name(const char *name) {
    int result = test_case->run_func();
    if (result != 0) {
        serial_puts("[TEST] FAILURE - System shutting down\n");
        system_shutdown();  // Immediate termination
    }
    return result;
}
```

---

## Adding a New Test Case

### Step 1: Create Test Source File

Create test file in appropriate directory:

```bash
# For subsystem-specific tests (preferred)
tests/<subsystem>/<subsystem>_test.c

# Examples:
tests/timer/timer_test.c
tests/interrupt/interrupt_test.c
```

### Step 2: Implement Test Function

```c
/* tests/foo/foo_test.c */
#include <stdint.h>
#include "arch/x86_64/serial.h"
#include "kernel/foo.h"  // Subsystem being tested

/**
 * run_foo_tests - Run foo subsystem tests
 *
 * Returns: Number of failed tests (0 = all passed)
 */
int run_foo_tests(void) {
    int failures = 0;

    serial_puts("\n========================================\n");
    serial_puts("  FOO Test Suite\n");
    serial_puts("========================================\n");

    // Test 1
    if (test_foo_feature() != 0) {
        failures++;
    }

    // Summary
    if (failures == 0) {
        serial_puts("  FOO: All tests PASSED\n");
    } else {
        serial_puts("  FOO: Some tests FAILED\n");
    }

    return failures;
}
```

### Step 3: Update Build System

**Edit Makefile:**

```makefile
# Add configuration flag
CFLAGS += -DCONFIG_FOO_TESTS=$(CONFIG_FOO_TESTS)

# Add test source (conditionally compiled)
FOO_TEST_SRC := tests/foo/foo_test.c
FOO_TEST_OBJ := $(BUILD_DIR)/foo_test.o

ifeq ($(CONFIG_FOO_TESTS),1)
TESTS_OBJS += $(FOO_TEST_OBJ)
endif

# Add compilation rule
ifeq ($(CONFIG_FOO_TESTS),1)
$(FOO_TEST_OBJ): $(FOO_TEST_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@
endif
```

**Edit kernel.config:**

```makefile
# Foo subsystem tests
CONFIG_FOO_TESTS?=0
```

### Step 4: Register Test in Framework

**Edit kernel/test.c:**

```c
#if CONFIG_FOO_TESTS
    { .name = "foo",
      .description = "Foo subsystem functionality tests",
      .run_func = run_foo_tests,
      .enabled = 1,
      .auto_run = 1 },  /* Set to 0 for manual-only tests */
#endif
```

### Step 5: Add Auto-Run Call Point (if auto_run=1)

**Edit arch/x86_64/main.c:**

```c
/* After foo_init() */
extern void foo_init(void);
foo_init();

#if CONFIG_FOO_TESTS
    if (test_should_run("foo")) {
        test_run_by_name("foo");
    }
#endif
```

### Step 6: Build and Test

```bash
# Enable test in .config or override
echo "CONFIG_FOO_TESTS=1" >> .config

# Build with test enabled
make clean
make

# Run the specific test
make run KERNEL_CMDLINE="test=foo"
```

---

## Deleting a Test Case

### Step 1: Remove from Test Registry

**Edit kernel/test.c:**

```c
// Remove or comment out the entry:
// #if CONFIG_FOO_TESTS
//     { .name = "foo", ... },
// #endif
```

### Step 2: Remove Auto-Run Call

**Edit arch/x86_64/main.c:**

```c
// Remove the conditional test block:
// #if CONFIG_FOO_TESTS
//     if (test_should_run("foo")) {
//         test_run_by_name("foo");
//     }
// #endif
```

### Step 3: (Optional) Remove Source Files

```bash
rm tests/foo/foo_test.c
```

### Step 4: (Optional) Remove from Build System

**Edit Makefile:**

```makefile
# Remove FOO_TEST_SRC, FOO_TEST_OBJ, and related rules
```

**Edit kernel.config:**

```makefile
# Remove CONFIG_FOO_TESTS line
```

---

## Running Test Cases

### Method 1: Direct QEMU Execution (Manual)

```bash
# Run all tests (distributed auto-run)
make run KERNEL_CMDLINE="test=all"

# Run specific test
make run KERNEL_CMDLINE="test=slab"

# Run tests in unified mode
make run KERNEL_CMDLINE="test=unified"

# Boot without tests (default)
make run KERNEL_CMDLINE="quote=\"Hello\""
```

**IMPORTANT NOTE: GRUB/QEMU Command Line Limitation**

When running in QEMU with GRUB, the multiboot info structure may not pass the command line correctly from the GRUB configuration to the kernel. This is a known limitation in some QEMU/GRUB combinations.

**Workaround Options:**

1. **Use Default Cmdline (for QEMU testing):**
   Edit `arch/x86_64/multiboot2.c` and set `default_cmdline` in the `use_default_cmdline` section:
   ```c
   const char *default_cmdline = "test=all";  // or test=slab, test=unified, etc.
   ```

2. **Use Real Hardware:** On real hardware with a proper bootloader, the KERNEL_CMDLINE in Makefile and GRUB configuration work correctly.

3. **Use Alternative Boot Methods:** Consider using direct multiboot without GRUB for testing purposes.

### Method 2: Using Test Scripts

```bash
# Basic boot test (1 CPU, no specific tests)
make test-boot

# Slab allocator test (2 CPUs)
make test-slab

# SMP boot test (4 CPUs)
make test-smp

# Run all integration tests
make test-all
```

### Method 3: Custom QEMU Invocation

```bash
# Build with specific config
make clean
make CONFIG_SLAB_TESTS=1 CONFIG_SPINLOCK_TESTS=1

# Run with custom cmdline
qemu-system-x86_64 -enable-kvm -M pc -m 128M -nographic \
    -cdrom emergence.iso -smp 2 \
    -device isa-debug-exit,iobase=0xB004,iosize=1
```

### Test Script Customization

To make test scripts support the new framework, update them to pass `KERNEL_CMDLINE`:

```bash
# In tests/slab/slab_test.sh
local test_cmdline="test=slab"
local output_file=$(KERNEL_CMDLINE="$test_cmdline" run_qemu_capture 2 10)
```

---

## Checking Test Results

### Method 1: Serial Console Output

Tests write results to serial console in real-time:

```
========================================
  SLAB Allocator Test Suite
========================================

[SLAB test] Single allocation test...
[SLAB test] Allocated 64-byte object at 0x...
[SLAB test] Single allocation test PASSED

...
========================================
  SLAB: All tests PASSED
========================================
```

### Method 2: Captured Log Files

Test scripts capture output to temporary files:

```bash
# Test script creates output file
local output_file=$(run_qemu_capture 2 10)

# View captured output
cat "$output_file"

# Search for specific patterns
grep "SLAB.*PASSED" "$output_file"
grep "FAILURE\|FAILED\|exception" "$output_file"
```

### Method 3: Exit Codes

Test scripts exit with status codes:

```bash
make test-slab
echo $?  # 0 = all passed, 1 = some failed
```

### Method 4: Test Framework Summary

The test framework prints a summary:

```
========================================
Tests complete
Summary: 5/5 tests passed
Result: ALL TESTS PASSED
========================================
```

### Common Result Patterns

| Pattern | Meaning |
|---------|---------|
| `ALL TESTS PASSED` | All assertions passed |
| `SOME TESTS FAILED` | One or more tests failed |
| `FAILURE - System shutting down` | Fail-fast triggered |
| `exception` or `fault` | Kernel panic/bug |
| `system is shutting down` | Clean shutdown |

---

## Migration Guide

### For Existing Tests

**PMM Tests** (currently inline in main.c):

1. Extract to `tests/pmm/pmm_test.c`
2. Create `run_pmm_tests()` function
3. Update Makefile to add PMM_TEST_SRC
4. Register in test_registry
5. Replace inline code with `test_should_run("pmm")` call

**Slab/Spinlock/NK Protection Tests**:

1. Already in separate files
2. Just add to test_registry
3. Replace conditional blocks with framework calls

### For Test Scripts

Update bash test scripts to use `KERNEL_CMDLINE`:

```bash
# Before (implicitly uses default cmdline)
local output_file=$(run_qemu_capture 2 10)

# After (explicitly specifies test)
local test_cmdline="test=slab"
local output_file=$(KERNEL_CMDLINE="$test_cmdline" run_qemu_capture 2 10)
```

---

## File Reference

### New Files (Post-Refactoring)

| File | Purpose |
|------|---------|
| `kernel/test.h` | Test framework public API |
| `kernel/test.c` | Test framework implementation |
| `tests/pmm/pmm_test.c` | Extracted PMM tests |

### Modified Files

| File | Changes |
|------|---------|
| `Makefile` | Add kernel/test.c, tests/pmm/pmm_test.c |
| `arch/x86_64/main.c` | Replace test blocks with framework calls |
| `arch/x86_64/multiboot2.c` | Export `cmdline_get_value()` |
| `arch/x86_64/multiboot2.h` | Add `cmdline_get_value()` prototype |

### Unchanged Files

| File | Reason |
|------|--------|
| `tests/spinlock/spinlock_test.c` | Already has `run_spinlock_tests()` |
| `tests/slab/slab_test.c` | Already has `run_slab_tests()` |
| `tests/nk_protection/nk_protection_test.c` | Already has `run_nk_protection_tests()` |
| `kernel.config` | CONFIG flags already control compilation |

---

## Quick Reference

### Add a new test (5 steps)

```bash
# 1. Create file
tests/mytest/mytest_test.c

# 2. Implement function
int run_mytest_tests(void) { ... }

# 3. Update Makefile
# Add CONFIG_MYTEST_TESTS, MY_TEST_SRC, compile rule

# 4. Register in kernel/test.c
{ .name = "mytest", .run_func = run_mytest_tests, ... }

# 5. Add auto-run call in main.c (if auto_run=1)
if (test_should_run("mytest")) test_run_by_name("mytest");
```

### Run tests

```bash
make run KERNEL_CMDLINE="test=all"        # All tests
make run KERNEL_CMDLINE="test=slab"       # Specific test
make run KERNEL_CMDLINE="test=unified"    # Unified mode
make run                                  # No tests
```

### Check results

```bash
# Watch serial output
make run KERNEL_CMDLINE="test=all" 2>&1 | tee boot.log

# Or use test scripts
make test-slab
cat /tmp/emergence_test_output_*.log
```
