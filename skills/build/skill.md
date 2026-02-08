---
name: build
description: Use when modifying kernel build configuration, adding new config options, or changing compile-time settings.
---

# Kernel Build Configuration Naming Conventions

All configuration options in `kernel.config` must follow strict naming prefixes based on their category:

## Configuration Prefixes

### `CONFIG_TESTS_*` - Test Configuration
All test-related options must use this prefix.

**Examples:**
- `CONFIG_TESTS_SPINLOCK` - Spinlock synchronization tests
- `CONFIG_TESTS_PMM` - Physical memory manager tests
- `CONFIG_TESTS_SLAB` - Slab allocator tests
- `CONFIG_TESTS_APIC_TIMER` - APIC timer interrupt tests
- `CONFIG_TESTS_BOOT` - Basic kernel boot verification
- `CONFIG_TESTS_SMP` - SMP startup and multi-CPU verification
- `CONFIG_TESTS_PCD` - Page Control Data tests
- `CONFIG_TESTS_MINILIBC` - Minimal string library tests
- `CONFIG_TESTS_USERMODE` - User mode syscall tests
- `CONFIG_TESTS_NK_INVARIANTS` - Nested Kernel invariants verification
- `CONFIG_TESTS_NK_READONLY_VISIBILITY` - Read-only mapping visibility tests
- `CONFIG_TESTS_NK_FAULT_INJECTION` - Nested kernel fault injection tests
- `CONFIG_TESTS_NK_TRAMPOLINE` - Monitor trampoline CR3 switching tests

**Pattern:** `CONFIG_TESTS_<feature_name>` or `CONFIG_TESTS_NK_<feature_name>`

### `CONFIG_DEBUG_*` - Debug Configuration
All debug and diagnostic options must use this prefix.

**Examples:**
- `CONFIG_DEBUG_SMP_AP` - SMP AP startup debug marks (H→G→3→A→P→L→X→D→S→Q→A→I→L→T→W)
- `CONFIG_DEBUG_PCD_STATS` - Page Control Data statistics dump
- `CONFIG_DEBUG_NK_INVARIANTS_VERBOSE` - Verbose Nested Kernel invariants output

**Pattern:** `CONFIG_DEBUG_<feature_name>`

### `CONFIG_NK_*` - Nested Kernel Configuration
Core nested kernel architecture options (non-debug, non-test).

**Examples:**
- `CONFIG_NK_WRITE_PROTECTION_VERIFY` - Verify Nested Kernel invariants at boot

**Pattern:** `CONFIG_NK_<feature_name>`

## Adding New Configuration Options

When adding new configuration options:

1. **Choose the correct prefix** based on the category above
2. **Add to kernel.config** with descriptive comment explaining what it does
3. **Add CFLAGS line in Makefile**: `CFLAGS += -DCONFIG_<NAME>=$(CONFIG_<NAME>)`
4. **Add conditional compilation** where appropriate:
   ```c
   #if CONFIG_<NAME>
   // code
   #endif
   ```
5. **Add help text** to Makefile help target:
   ```makefile
   @echo "  make CONFIG_<NAME>=<value>  - Description"
   ```

## Test Configuration vs Test Implementation

- **`CONFIG_TESTS_*` options** in `kernel.config` - enable/disable test compilation
- **Test source files** in `tests/<test_name>/` - Python test scripts and C test code
- **Test registry** in `kernel/test.c` - maps test names to run functions

## Configuration Value Types

- **Boolean flags**: `?=` allows make command line override
- **Enabled by default**: `?=` with `1`
- **Disabled by default**: `?=` with `0` or empty

## Common Patterns

### Test Options (CONFIG_TESTS_*)
```makefile
# <feature> tests - <brief description>
# Set to 1 to enable, 0 to disable
CONFIG_TESTS_<FEATURE> ?= 0
```

### Debug Options (CONFIG_DEBUG_*)
```makefile
# <feature> debug - <what it outputs>
# Set to 1 to enable, 0 to disable
CONFIG_DEBUG_<FEATURE> ?= 0
```

### Core Options (CONFIG_NK_*)
```makefile
# <feature> - <what it controls>
# Set to 1 to enable, 0 to disable
CONFIG_NK_<FEATURE> ?= 1
```

## Configuration Sections in kernel.config

The file is organized into sections:
1. **Test Configuration** - All `CONFIG_TESTS_*` options
2. **Nested Kernel Configuration** - Core `CONFIG_NK_*` options
3. **Debug Configuration** - All `CONFIG_DEBUG_*` options
