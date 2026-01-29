# Build Configuration System

## Overview

The Emergence Kernel uses a flexible configuration system that allows you to control which features are enabled at compile time. This system supports:

- **Default configuration** (`kernel.config`) - Version-controlled defaults
- **Local override** (`.config`) - Per-developer settings (not committed to git)
- **Command-line** - Temporary overrides for single builds

## Configuration Files

### kernel.config

This file contains the default configuration values and is committed to version control. It serves as the baseline for all builds.

Location: `/opt/workbench/os/gckernel/claude/kernel.config`

### .config

This optional file allows you to override default values for your local development environment. It is **not** committed to git (see `.gitignore`).

To create a local override:
```bash
cp kernel.config .config
# Edit .config with your preferred settings
```

### Configuration Priority

When building, configuration values are resolved in this order (later values override earlier ones):

1. **kernel.config** - Default values
2. **.config** - Local override values
3. **Command-line** - `make CONFIG_XXX=value` - Temporary override

Example:
```bash
# kernel.config has: CONFIG_SPINLOCK_TESTS ?= 0
# .config has: CONFIG_SPINLOCK_TESTS ?= 1
# Command-line: make CONFIG_SPINLOCK_TESTS=0
# Result: 0 (command-line wins)
```

## Configuration Options

### Test Configuration

| Option | Default | Description |
|--------|---------|-------------|
| `CONFIG_SPINLOCK_TESTS` | 0 | Enable comprehensive spin lock test suite |
| `CONFIG_PMM_TESTS` | 0 | Enable physical memory manager allocation tests |
| `CONFIG_APIC_TIMER_TEST` | 1 | Enable APIC timer test (outputs mathematician quotes) |

#### CONFIG_SPINLOCK_TESTS

Enables the spin lock test suite that runs during kernel boot. When enabled, the kernel will:

- Run 10 different spin lock tests
- Test single-CPU and multi-CPU scenarios
- Verify basic locks, IRQ-safe locks, and read-write locks
- Output detailed test results

**Output when enabled:**
```
[ Spin lock tests ] Starting spin lock test suite...
[ Spin lock tests ] Test 1: Basic lock operations... PASSED
[ Spin lock tests ] Test 2: Trylock behavior... PASSED
...
[ Spin lock tests ] Result: ALL TESTS PASSED
```

#### CONFIG_PMM_TESTS

Enables the Physical Memory Manager test suite. When enabled, tests:

- Single page allocations
- Multi-page (order-based) allocations
- Memory coalescing on free
- Statistics accuracy

**Output when enabled:**
```
[ PMM tests ] Running allocation tests...
[ PMM tests ] Allocated page1 at 0x200000, page2 at 0x201000
[ PMM tests ] Allocated 32KB block at 0x208000
[ PMM tests ] Tests complete
```

#### CONFIG_APIC_TIMER_TEST

Enables the APIC timer test that outputs famous mathematician quotes via periodic interrupts.

**Output when enabled:**
```
[ APIC tests ] 1. Mathematics is queen of sciences. - Gauss
[ APIC tests ] 2. Pure math is poetry of logic. - Einstein
[ APIC tests ] 3. Math reveals secrets to lovers. - Cantor
[ APIC tests ] 4. Proposing questions exceeds solving. - Cantor
[ APIC tests ] 5. God created natural numbers. - Kronecker
```

### Debug Configuration

| Option | Default | Description |
|--------|---------|-------------|
| `CONFIG_SMP_AP_DEBUG` | 0 | Enable AP startup debug marks on serial output |

#### CONFIG_SMP_AP_DEBUG

When enabled, outputs debug marks during AP (Application Processor) startup. Each mark represents a stage in the boot process:

| Mark | Stage | Description |
|------|-------|-------------|
| H | Real Mode | Trampoline entry, stack set up |
| G | GDT32 | 32-bit GDT loaded |
| 3 | Protected Mode | Entered 32-bit protected mode |
| A | PAE | Physical Address Extension enabled |
| P | Page Tables | CR3 loaded, TLB flushed |
| L | Long Mode | IA32_EFER.LME set |
| X | Paging | CR0.PG set (Long Mode active) |
| D | GDT64 | 64-bit GDT loaded |
| S | Stub | 64-bit stub entry reached |
| Q | Stack | AP stack configured |
| A | Segments | Segment registers cleared |
| I | IDT Load | Before IDT load |
| L | IDT Done | After IDT load |
| T | Jump | About to jump to ap_start |
| W | Bridge | test_ap_start bridge reached |

**Example output:** `HG3APLXDSQAILTW`

## Usage Examples

### Building with Defaults

```bash
make
# Uses all values from kernel.config
```

### Building with Custom Options

```bash
# Enable spin lock tests for this build only
make CONFIG_SPINLOCK_TESTS=1

# Enable PMM tests and AP debug marks
make CONFIG_PMM_TESTS=1 CONFIG_SMP_AP_DEBUG=1

# Disable APIC timer test
make CONFIG_APIC_TIMER_TEST=0
```

### Creating a Persistent Local Configuration

```bash
# Copy the default config
cp kernel.config .config

# Edit with your preferred settings
nano .config
```

Example `.config`:
```makefile
# My development configuration
CONFIG_SPINLOCK_TESTS ?= 1
CONFIG_PMM_TESTS ?= 1
CONFIG_APIC_TIMER_TEST ?= 0
CONFIG_SMP_AP_DEBUG ?= 1
```

### Checking Active Configuration

To see which configuration values are being used in a build:

```bash
make clean
make 2>&1 | grep "CONFIG_"
```

Example output:
```
gcc ... -DCONFIG_SPINLOCK_TESTS=0 -DCONFIG_PMM_TESTS=0 -DCONFIG_SMP_AP_DEBUG=0 -DCONFIG_APIC_TIMER_TEST=1 -c ...
```

## Advanced Usage

### Conditional Compilation in Code

The configuration system uses C preprocessor conditionals. In your code:

```c
#if CONFIG_SPINLOCK_TESTS
    // This code only compiles when CONFIG_SPINLOCK_TESTS=1
    run_spinlock_tests();
#endif

#if CONFIG_SMP_AP_DEBUG
    serial_puts("Debug: AP reached stage X\n");
#endif
```

### Adding New Configuration Options

To add a new configuration option:

1. **Add to `kernel.config`:**
   ```makefile
   # Set to 1 to enable my feature, 0 to disable
   CONFIG_MY_FEATURE ?= 0
   ```

2. **Add to `Makefile` CFLAGS:**
   ```makefile
   CFLAGS += -DCONFIG_MY_FEATURE=$(CONFIG_MY_FEATURE)
   ```

3. **Use in code with conditional compilation:**
   ```c
   #if CONFIG_MY_FEATURE
       my_feature_code();
   #endif
   ```

4. **Document in this file** and the READMEs

## Testing

### Verifying Configuration Changes

After modifying configuration, verify the changes take effect:

```bash
# Clean build to ensure all files are recompiled
make clean

# Build and check compiler flags
make 2>&1 | grep -E "CONFIG_"

# Run and verify expected behavior
make run
```

### Recommended Test Configurations

**Minimal (fastest boot, no tests):**
```bash
# .config
CONFIG_SPINLOCK_TESTS ?= 0
CONFIG_PMM_TESTS ?= 0
CONFIG_APIC_TIMER_TEST ?= 0
CONFIG_SMP_AP_DEBUG ?= 0
```

**Development (most tests enabled):**
```bash
# .config
CONFIG_SPINLOCK_TESTS ?= 1
CONFIG_PMM_TESTS ?= 1
CONFIG_APIC_TIMER_TEST ?= 1
CONFIG_SMP_AP_DEBUG ?= 0
```

**Debugging (full debug output):**
```bash
# .config
CONFIG_SPINLOCK_TESTS ?= 1
CONFIG_PMM_TESTS ?= 1
CONFIG_APIC_TIMER_TEST ?= 1
CONFIG_SMP_AP_DEBUG ?= 1
```

## Troubleshooting

### Configuration Not Taking Effect

If your configuration changes don't seem to work:

1. **Clean build:**
   ```bash
   make clean
   make
   ```

2. **Check compiler flags:**
   ```bash
   make 2>&1 | grep "CONFIG_"
   ```

3. **Verify file priority:**
   - Check if `.config` exists and contains your changes
   - Check if command-line is overriding your settings

4. **Check syntax:**
   - Use `?=` for deferred assignment
   - Don't use spaces around `=` in Makefiles

### Unexpected Test Output

If you see test output but didn't enable tests:

1. Check for stale `.config` file:
   ```bash
   cat .config
   rm .config  # Remove if needed
   ```

2. Verify `kernel.config` values:
   ```bash
   cat kernel.config
   ```

3. Rebuild from clean:
   ```bash
   make clean
   make
   ```
