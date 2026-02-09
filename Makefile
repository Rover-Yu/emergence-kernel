# Emergence Kernel Makefile

KERNEL := emergence
ISO := emergence.iso
BUILD_DIR := build
ISO_DIR := isodir

# Kernel command line (passed via GRUB multiboot2)
# Default: Learning with Every Boot
KERNEL_CMDLINE ?= motto="Learning with Every Boot"

# ========================================================================
# Configuration
# ========================================================================
# Load kernel configuration
# Priority: .config (local override) > kernel.config (default)
-include .config
include kernel.config

# Tools
CC := gcc
AS := as
LD := ld
GRUB_MKRESCUE := grub-mkrescue

# Verbosity: V=1 for verbose output, V=0 for quiet (default)
V ?= 0
ifeq ($(V),1)
Q =
else
Q = @
endif

# Flags (x86_64 with multiboot support)
CFLAGS := -ffreestanding -O2 -Wall -g -nostdlib -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -I.

# Test configuration options (sorted by kernel.config order)
CFLAGS += -DCONFIG_TESTS_SPINLOCK=$(CONFIG_TESTS_SPINLOCK)
CFLAGS += -DCONFIG_TESTS_PMM=$(CONFIG_TESTS_PMM)
CFLAGS += -DCONFIG_TESTS_SLAB=$(CONFIG_TESTS_SLAB)
CFLAGS += -DCONFIG_TESTS_APIC_TIMER=$(CONFIG_TESTS_APIC_TIMER)
CFLAGS += -DCONFIG_TESTS_BOOT=$(CONFIG_TESTS_BOOT)
CFLAGS += -DCONFIG_TESTS_SMP=$(CONFIG_TESTS_SMP)
CFLAGS += -DCONFIG_TESTS_PCD=$(CONFIG_TESTS_PCD)
CFLAGS += -DCONFIG_TESTS_MINILIBC=$(CONFIG_TESTS_MINILIBC)
CFLAGS += -DCONFIG_TESTS_USERMODE=$(CONFIG_TESTS_USERMODE)
CFLAGS += -DCONFIG_TESTS_NK_INVARIANTS=$(CONFIG_TESTS_NK_INVARIANTS)
CFLAGS += -DCONFIG_TESTS_NK_READONLY_VISIBILITY=$(CONFIG_TESTS_NK_READONLY_VISIBILITY)
CFLAGS += -DCONFIG_TESTS_NK_FAULT_INJECTION=$(CONFIG_TESTS_NK_FAULT_INJECTION)
CFLAGS += -DCONFIG_TESTS_NK_TRAMPOLINE=$(CONFIG_TESTS_NK_TRAMPOLINE)
CFLAGS += -DCONFIG_TESTS_NK_INVARIANTS_VERIFY=$(CONFIG_TESTS_NK_INVARIANTS_VERIFY)

# Debug configuration options (sorted by kernel.config order)
CFLAGS += -DCONFIG_DEBUG_SMP_AP=$(CONFIG_DEBUG_SMP_AP)
CFLAGS += -DCONFIG_DEBUG_PCD_STATS=$(CONFIG_DEBUG_PCD_STATS)
CFLAGS += -DCONFIG_DEBUG_NK_INVARIANTS_VERBOSE=$(CONFIG_DEBUG_NK_INVARIANTS_VERBOSE)
LDFLAGS := -nostdlib -m elf_x86_64

# Architecture-specific sources (x86_64)
ARCH_DIR := arch/x86_64
ARCH_BOOT_SRC := $(ARCH_DIR)/boot.S $(ARCH_DIR)/isr.S $(ARCH_DIR)/monitor/monitor_call.S $(ARCH_DIR)/userprog.S $(ARCH_DIR)/syscall_entry.S
ARCH_LINKER := $(ARCH_DIR)/linker.ld
ARCH_C_SRCS := $(ARCH_DIR)/main.c $(ARCH_DIR)/smp.c $(ARCH_DIR)/multiboot2.c \
               $(ARCH_DIR)/vga.c $(ARCH_DIR)/serial_driver.c $(ARCH_DIR)/apic.c \
               $(ARCH_DIR)/acpi.c $(ARCH_DIR)/idt.c $(ARCH_DIR)/timer.c $(ARCH_DIR)/rtc.c \
               $(ARCH_DIR)/ipi.c $(ARCH_DIR)/power.c $(ARCH_DIR)/syscall.c

# AP Trampoline (assembled as part of kernel, uses PIC)
TRAMPOLINE_SRC := $(ARCH_DIR)/ap_trampoline.S
TRAMPOLINE_OBJ := $(BUILD_DIR)/ap_trampoline.o

# Architecture-independent kernel sources
KERNEL_DIR := kernel
KERNEL_C_SRCS := $(KERNEL_DIR)/device.c $(KERNEL_DIR)/pmm.c $(KERNEL_DIR)/pcd.c \
                 $(KERNEL_DIR)/slab.c $(KERNEL_DIR)/test.c \
                 $(KERNEL_DIR)/monitor/monitor.c

# Minilibc sources
MINILIBC_C_SRCS := lib/minilibc/string.c
MINILIBC_OBJS := $(patsubst lib/minilibc/%.c,$(BUILD_DIR)/minilibc_%.o,$(MINILIBC_C_SRCS))

# Spinlock test sources (conditionally compiled)
SPINLOCK_TEST_SRC := tests/spinlock/spinlock_test.c
SPINLOCK_TEST_OBJ := $(BUILD_DIR)/kernel_spinlock_test.o

# Slab allocator test sources (conditionally compiled)
SLAB_TEST_SRC := tests/slab/slab_test.c
SLAB_TEST_OBJ := $(BUILD_DIR)/kernel_slab_test.o

# PMM test sources (conditionally compiled)
PMM_TEST_SRC := tests/pmm/pmm_test.c
PMM_TEST_OBJ := $(BUILD_DIR)/kernel_pmm_test.o

# APIC timer test sources (conditionally compiled)
TIMER_TEST_SRC := tests/timer/timer_test.c
TIMER_TEST_OBJ := $(BUILD_DIR)/kernel_timer_test.o

# Nested kernel fault injection test sources (conditionally compiled)
NK_PROTECTION_TEST_SRC := tests/nk_fault_injection/nk_fault_injection_test.c
NK_PROTECTION_TEST_OBJ := $(BUILD_DIR)/nk_fault_injection_test.o

# Boot test sources (conditionally compiled)
BOOT_TEST_SRC := tests/boot/boot_test.c
BOOT_TEST_OBJ := $(BUILD_DIR)/kernel_boot_test.o

# SMP boot test sources (conditionally compiled)
SMP_TEST_SRC := tests/smp/smp_boot_test.c
SMP_TEST_OBJ := $(BUILD_DIR)/kernel_smp_test.o

# PCD test sources (conditionally compiled)
PCD_TEST_SRC := tests/pcd/pcd_test.c
PCD_TEST_OBJ := $(BUILD_DIR)/kernel_pcd_test.o

# Nested Kernel invariants test sources (conditionally compiled)
NK_INVARIANTS_TEST_SRC := tests/nested_kernel_invariants/nested_kernel_invariants_test.c
NK_INVARIANTS_TEST_OBJ := $(BUILD_DIR)/kernel_nk_invariants_test.o

# Read-only visibility test sources (conditionally compiled)
READONLY_VISIBILITY_TEST_SRC := tests/readonly_visibility/readonly_visibility_test.c
READONLY_VISIBILITY_TEST_OBJ := $(BUILD_DIR)/kernel_readonly_visibility_test.o

# User mode syscall test sources (conditionally compiled)
USERMODE_TEST_SRC := tests/usermode/usermode_test.c
USERMODE_TEST_OBJ := $(BUILD_DIR)/kernel_usermode_test.o

# Monitor trampoline test sources (conditionally compiled)
MONITOR_TRAMPOLINE_TEST_SRC := tests/monitor_trampoline/monitor_trampoline_test.c
MONITOR_TRAMPOLINE_TEST_OBJ := $(BUILD_DIR)/monitor_trampoline_test.o

# Test sources (reference only, not compiled into kernel)
# These test files are kept for documentation purposes
TESTS_DIR := tests
TESTS_C_SRCS :=  # Intentionally empty - test files are reference only

# All C sources
C_SRCS := $(ARCH_C_SRCS) $(KERNEL_C_SRCS)

# Objects
ARCH_BOOT_OBJ := $(patsubst $(ARCH_DIR)/%.S,$(BUILD_DIR)/boot_%.o,$(ARCH_DIR)/boot.S) \
                $(patsubst $(ARCH_DIR)/%.S,$(BUILD_DIR)/isr_%.o,$(ARCH_DIR)/isr.S) \
                $(BUILD_DIR)/boot_monitor_monitor_call.o \
                $(BUILD_DIR)/boot_syscall_entry.o \
                $(BUILD_DIR)/boot_userprog.o
ARCH_OBJS := $(patsubst $(ARCH_DIR)/%.c,$(BUILD_DIR)/arch_%.o,$(ARCH_C_SRCS))
KERNEL_OBJS := $(patsubst $(KERNEL_DIR)/%.c,$(BUILD_DIR)/kernel_%.o,$(KERNEL_C_SRCS))

# Initialize TESTS_OBJS with reference test sources (currently empty)
TESTS_OBJS := $(patsubst $(TESTS_DIR)/%.c,$(BUILD_DIR)/test_%.o,$(TESTS_C_SRCS))

# Conditionally include test objects (must be BEFORE OBJS is defined)
ifeq ($(CONFIG_TESTS_SPINLOCK),1)
TESTS_OBJS += $(SPINLOCK_TEST_OBJ)
endif
ifeq ($(CONFIG_TESTS_PMM),1)
TESTS_OBJS += $(PMM_TEST_OBJ)
endif
ifeq ($(CONFIG_TESTS_SLAB),1)
TESTS_OBJS += $(SLAB_TEST_OBJ)
endif
ifeq ($(CONFIG_TESTS_APIC_TIMER),1)
TESTS_OBJS += $(TIMER_TEST_OBJ)
endif
ifeq ($(CONFIG_TESTS_NK_FAULT_INJECTION),1)
TESTS_OBJS += $(NK_PROTECTION_TEST_OBJ)
endif
ifeq ($(CONFIG_TESTS_BOOT),1)
TESTS_OBJS += $(BOOT_TEST_OBJ)
endif
ifeq ($(CONFIG_TESTS_SMP),1)
TESTS_OBJS += $(SMP_TEST_OBJ)
endif
ifeq ($(CONFIG_TESTS_PCD),1)
TESTS_OBJS += $(PCD_TEST_OBJ)
endif
ifeq ($(CONFIG_TESTS_NK_INVARIANTS),1)
TESTS_OBJS += $(NK_INVARIANTS_TEST_OBJ)
endif
ifeq ($(CONFIG_TESTS_NK_READONLY_VISIBILITY),1)
TESTS_OBJS += $(READONLY_VISIBILITY_TEST_OBJ)
endif
ifeq ($(CONFIG_TESTS_USERMODE),1)
TESTS_OBJS += $(USERMODE_TEST_OBJ)
endif

# Generated command line object
CMDLINE_OBJ := $(BUILD_DIR)/kernel_cmdline_source.o

# Add cmdline object to OBJS (TESTS_OBJS now includes conditionally compiled tests)
OBJS := $(ARCH_BOOT_OBJ) $(ARCH_OBJS) $(KERNEL_OBJS) $(MINILIBC_OBJS) $(TESTS_OBJS) $(TRAMPOLINE_OBJ) $(CMDLINE_OBJ)

KERNEL_ELF := $(BUILD_DIR)/$(KERNEL).elf

.PHONY: all clean run run-debug test test-all test-boot test-apic-timer test-smp test-pcd test-slab test-nested-kernel test-nk-protection test-readonly-visibility test-usermode help

all: $(ISO)

help:
	@echo "Emergence Kernel Make Targets"
	@echo "=============================="
	@echo ""
	@echo "Build targets:"
	@echo "  all              - Build kernel ISO (quiet output)"
	@echo "  clean            - Remove build artifacts"
	@echo ""
	@echo "Verbosity:"
	@echo "  V=1              - Show full compiler commands"
	@echo "  Example: make V=1"
	@echo ""
	@echo "Run targets:"
	@echo "  run              - Run kernel in QEMU (4 CPUs, 128M RAM)"
	@echo "  run-debug        - Run kernel in QEMU with GDB server on :1234"
	@echo ""
	@echo "Test targets:"
	@echo "  test             - Run all tests (test-all)"
	@echo "  test-all         - Run complete test suite"
	@echo "  test-boot        - Basic kernel boot test (1 CPU)"
	@echo "  test-apic-timer  - APIC timer interrupt test"
	@echo "  test-smp         - SMP boot test (2 CPUs)"
	@echo "  test-pcd         - Page Control Data test"
	@echo "  test-slab        - Slab allocator test"
	@echo "  test-nested-kernel     - Nested Kernel invariants test"
	@echo "  test-nk-protection     - Nested Kernel mappings protection test"
	@echo "  test-readonly-visibility - Read-only visibility test"
	@echo "  test-usermode          - User mode syscall test (KVM enabled)"
	@echo ""
	@echo "Build options (override kernel.config):"
	@echo "  make CONFIG_TESTS_SPINLOCK=1             - Enable spinlock tests"
	@echo "  make CONFIG_TESTS_PMM=1                  - Enable PMM tests"
	@echo "  make CONFIG_TESTS_SLAB=1                  - Enable slab allocator tests"
	@echo "  make CONFIG_TESTS_APIC_TIMER=1            - Enable APIC timer test"
	@echo "  make CONFIG_TESTS_BOOT=1                  - Enable boot tests"
	@echo "  make CONFIG_TESTS_SMP=1                   - Enable SMP tests"
	@echo "  make CONFIG_TESTS_PCD=1                   - Enable PCD tests"
	@echo "  make CONFIG_TESTS_MINILIBC=1              - Enable minilibc tests"
	@echo "  make CONFIG_TESTS_USERMODE=1              - Enable user mode syscall tests"
	@echo "  make CONFIG_TESTS_NK_INVARIANTS=1          - Enable NK invariants tests"
	@echo "  make CONFIG_TESTS_NK_READONLY_VISIBILITY=1 - Enable NK read-only visibility tests"
	@echo "  make CONFIG_TESTS_NK_FAULT_INJECTION=1    - Enable NK fault injection tests"
	@echo "  make CONFIG_TESTS_NK_TRAMPOLINE=1         - Enable NK trampoline test"
	@echo ""
	@echo "Debug options:"
	@echo "  make CONFIG_DEBUG_SMP_AP=1                - Enable SMP AP debug marks"
	@echo "  make CONFIG_DEBUG_PCD_STATS=1             - Show PCD statistics"
	@echo "  make CONFIG_DEBUG_NK_INVARIANTS_VERBOSE=1 - Verbose NK invariants output"
	@echo ""
	@echo "Nested Kernel options:"
	@echo "  make CONFIG_TESTS_NK_INVARIANTS_VERIFY=1  - Verify write protection"

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(ISO_DIR):
	mkdir -p $(ISO_DIR)/boot/grub

.tmp:
	mkdir -p .tmp

# Compile architecture-specific boot assembly
$(BUILD_DIR)/boot_%.o: $(ARCH_DIR)/boot.S | $(BUILD_DIR)
	@echo "  AS      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

# Compile ISR assembly
$(BUILD_DIR)/isr_%.o: $(ARCH_DIR)/isr.S | $(BUILD_DIR)
	@echo "  AS      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

# Compile monitor assembly files
$(BUILD_DIR)/boot_monitor_%.o: $(ARCH_DIR)/monitor/%.S | $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)
	@echo "  AS      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@


# Compile syscall_entry assembly
$(BUILD_DIR)/boot_syscall_entry.o: $(ARCH_DIR)/syscall_entry.S | $(BUILD_DIR)
	@echo "  AS      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

# Compile userprog assembly
$(BUILD_DIR)/boot_userprog.o: $(ARCH_DIR)/userprog.S | $(BUILD_DIR)
	@echo "  AS      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

# Compile architecture-specific C files
$(BUILD_DIR)/arch_%.o: $(ARCH_DIR)/%.c | $(BUILD_DIR)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

# Compile architecture-independent kernel C files
$(BUILD_DIR)/kernel_%.o: $(KERNEL_DIR)/%.c | $(BUILD_DIR)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel_monitor/%.o: $(KERNEL_DIR)/monitor/%.c | $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)/kernel_monitor
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

# Compile minilibc C files
$(BUILD_DIR)/minilibc_%.o: lib/minilibc/%.c | $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

# Compile spinlock test (from tests/spinlock/) - only if enabled
ifeq ($(CONFIG_TESTS_SPINLOCK),1)
$(SPINLOCK_TEST_OBJ): $(SPINLOCK_TEST_SRC) | $(BUILD_DIR)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@
endif

# Compile slab allocator test (from tests/slab/) - only if enabled
ifeq ($(CONFIG_TESTS_SLAB),1)
$(SLAB_TEST_OBJ): $(SLAB_TEST_SRC) | $(BUILD_DIR)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@
endif

# Compile PMM test (from tests/pmm/) - only if enabled
ifeq ($(CONFIG_TESTS_PMM),1)
$(PMM_TEST_OBJ): $(PMM_TEST_SRC) | $(BUILD_DIR)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@
endif

# Compile APIC timer test (from tests/timer/) - only if enabled
ifeq ($(CONFIG_TESTS_APIC_TIMER),1)
$(TIMER_TEST_OBJ): $(TIMER_TEST_SRC) | $(BUILD_DIR)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@
endif

# Compile nested kernel fault injection test (from tests/nk_fault_injection/) - only if enabled
ifeq ($(CONFIG_TESTS_NK_FAULT_INJECTION),1)
$(NK_PROTECTION_TEST_OBJ): $(NK_PROTECTION_TEST_SRC) | $(BUILD_DIR)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@
endif

# Compile boot test (from tests/boot/) - only if enabled
ifeq ($(CONFIG_TESTS_BOOT),1)
$(BOOT_TEST_OBJ): $(BOOT_TEST_SRC) | $(BUILD_DIR)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@
endif

# Compile SMP boot test (from tests/smp/) - only if enabled
ifeq ($(CONFIG_TESTS_SMP),1)
$(SMP_TEST_OBJ): $(SMP_TEST_SRC) | $(BUILD_DIR)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@
endif

# Compile PCD test (from tests/pcd/) - only if enabled
ifeq ($(CONFIG_TESTS_PCD),1)
$(PCD_TEST_OBJ): $(PCD_TEST_SRC) | $(BUILD_DIR)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@
endif

# Compile nested kernel invariants test (from tests/nested_kernel_invariants/) - only if enabled
ifeq ($(CONFIG_TESTS_NK_INVARIANTS),1)
$(NK_INVARIANTS_TEST_OBJ): $(NK_INVARIANTS_TEST_SRC) | $(BUILD_DIR)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@
endif

# Compile read-only visibility test (from tests/readonly_visibility/) - only if enabled
ifeq ($(CONFIG_TESTS_NK_READONLY_VISIBILITY),1)
$(READONLY_VISIBILITY_TEST_OBJ): $(READONLY_VISIBILITY_TEST_SRC) | $(BUILD_DIR)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@
endif

# Compile usermode test (from tests/usermode/) - only if enabled
ifeq ($(CONFIG_TESTS_USERMODE),1)
$(USERMODE_TEST_OBJ): $(USERMODE_TEST_SRC) | $(BUILD_DIR)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@
endif

# Compile test C files
$(BUILD_DIR)/test_%.o: $(TESTS_DIR)/%.c | $(BUILD_DIR)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

# Compile AP trampoline (as 64-bit assembly, uses PIC)
# Use CC to get C preprocessor for conditional compilation
$(TRAMPOLINE_OBJ): $(TRAMPOLINE_SRC) | $(BUILD_DIR)
	@echo "  AS      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

# Compile generated cmdline source (always rebuild when KERNEL_CMDLINE changes)
$(BUILD_DIR)/kernel_cmdline_source.o: always-rebuild-cmdline
	@echo "  CC      cmdline_source.c"
	$(Q)$(CC) $(CFLAGS) -c $(CMDLINE_SOURCE) -o $@

# Link kernel (trampoline is included as regular object)
$(KERNEL_ELF): $(OBJS)
	@echo "  LD      $@"
	$(Q)$(LD) $(LDFLAGS) -T $(ARCH_LINKER) $^ -o $@

# Create ISO
# Generate embedded command line source file
CMDLINE_SOURCE := $(BUILD_DIR)/cmdline_source.c

.PHONY: always-rebuild-cmdline
always-rebuild-cmdline:
	@mkdir -p $(BUILD_DIR)
	@echo "/* Auto-generated command line source */" > $(CMDLINE_SOURCE)
	@echo "#include <stddef.h>" >> $(CMDLINE_SOURCE)
	@echo "const char embedded_cmdline[] = \"$(KERNEL_CMDLINE)\";" >> $(CMDLINE_SOURCE)

$(ISO): $(KERNEL_ELF) always-rebuild-cmdline | $(ISO_DIR) .tmp
	cp $(KERNEL_ELF) $(ISO_DIR)/boot/$(KERNEL).elf
	echo 'set timeout=0' > $(ISO_DIR)/boot/grub/grub.cfg
	echo 'set default=0' >> $(ISO_DIR)/boot/grub/grub.cfg
	echo 'menuentry "Emergence Kernel" {' >> $(ISO_DIR)/boot/grub/grub.cfg
	echo '    multiboot2 /boot/$(KERNEL).elf $(KERNEL_CMDLINE)' >> $(ISO_DIR)/boot/grub/grub.cfg
	echo '    boot' >> $(ISO_DIR)/boot/grub/grub.cfg
	echo '}' >> $(ISO_DIR)/boot/grub/grub.cfg
	env TMPDIR=$(PWD)/.tmp $(GRUB_MKRESCUE) -o $@ $(ISO_DIR)

run: $(ISO)
	@python3 tests/run.py --timeout 8 || exit 0

run-debug: $(ISO)
	qemu-system-x86_64 -enable-kvm -M pc -m 128M -nographic -cdrom $(ISO) -smp 4 -s -S -device isa-debug-exit,iobase=0xB004,iosize=1 || exit 0

clean:
	rm -rf $(BUILD_DIR) $(ISO_DIR) $(ISO)
	rm -f ./emergence_test_* 2>/dev/null || true
	rm -f /tmp/emergence_* 2>/dev/null || true

# Test targets (Python 3 only)
test: test-all

test-all:
	@$(MAKE) all
	@echo "Running Emergence Kernel test suite..."
	@python3 tests/run_all_tests.py

test-boot:
	@$(MAKE) all
	@echo "Running Basic Kernel Boot Test..."
	@python3 tests/boot/boot_test.py

test-apic-timer:
	@$(MAKE) all
	@echo "Running APIC Timer Test..."
	@python3 tests/timer/apic_timer_test.py

test-smp:
	@$(MAKE) all
	@echo "Running SMP Boot Test..."
	@python3 tests/smp/smp_boot_test.py --cpus 4

test-pcd:
	@$(MAKE) all
	@echo "Running Page Control Data (PCD) Test..."
	@python3 tests/pcd/pcd_test.py

test-slab:
	@$(MAKE) all
	@echo "Running Slab Allocator Test..."
	@python3 tests/slab/slab_test.py

test-nested-kernel:
	@echo "Running Nested Kernel Invariants Test..."
	@python3 tests/nested_kernel_invariants/nested_kernel_invariants_test.py

test-nk-fault-injection:
	@echo "Running Nested Kernel Fault Injection Test..."
	@python3 tests/nk_fault_injection/nk_fault_injection_test.py

test-readonly-visibility:
	@echo "Running Read-Only Visibility Test..."
	@python3 tests/readonly_visibility/readonly_visibility_test.py

test-usermode:
	@echo "Running User Mode Syscall Test..."
	@$(MAKE) all
	@python3 tests/usermode/usermode_test.py --kvm
