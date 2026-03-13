# Emergence Kernel Makefile

.DEFAULT_GOAL := all

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
CFLAGS += -DCONFIG_TESTS_SMP_MONITOR_STRESS=$(CONFIG_TESTS_SMP_MONITOR_STRESS)
CFLAGS += -DCONFIG_TESTS_SCHED=$(CONFIG_TESTS_SCHED)
CFLAGS += -DCONFIG_TESTS_SYSCALL=$(CONFIG_TESTS_SYSCALL)
CFLAGS += -DCONFIG_TESTS_KMAP=$(CONFIG_TESTS_KMAP)

# Debug configuration options (sorted by kernel.config order)
CFLAGS += -DCONFIG_DEBUG_SMP_AP=$(CONFIG_DEBUG_SMP_AP)
CFLAGS += -DCONFIG_DEBUG_PCD_STATS=$(CONFIG_DEBUG_PCD_STATS)
CFLAGS += -DCONFIG_DEBUG_NK_INVARIANTS_VERBOSE=$(CONFIG_DEBUG_NK_INVARIANTS_VERBOSE)
LDFLAGS := -nostdlib -m elf_x86_64

# Config tracking: force rebuild when config changes
# Create a hash of the current config flags to detect changes
CONFIG_HASH_FILE := $(BUILD_DIR)/.config_hash
CONFIG_HASH := $(shell echo "$(CFLAGS)" | md5sum 2>/dev/null | cut -d' ' -f1)

# Create config hash file if it doesn't exist or has changed
$(CONFIG_HASH_FILE): | $(BUILD_DIR)
	@echo "$(CONFIG_HASH)" > $@.tmp
	@if [ ! -f $@ ] || ! cmp -s $@ $@.tmp; then \
		echo "  CONFIG  changed, forcing rebuild"; \
		mv $@.tmp $@; \
	else \
		rm -f $@.tmp; \
	fi

# Make all compiled objects depend on config hash
CONFIG_DEP := $(CONFIG_HASH_FILE)

# Architecture-specific sources (x86_64)
ARCH_DIR := arch/x86_64
ARCH_BOOT_SRC := $(ARCH_DIR)/boot.S $(ARCH_DIR)/isr.S $(ARCH_DIR)/monitor/monitor_call.S $(ARCH_DIR)/userprog.S $(ARCH_DIR)/syscall_entry.S $(ARCH_DIR)/context.S
ARCH_TEST_SRC := $(ARCH_DIR)/syscall_test.S
ARCH_LINKER := $(ARCH_DIR)/linker.ld
ARCH_C_SRCS := $(ARCH_DIR)/main.c $(ARCH_DIR)/smp.c $(ARCH_DIR)/multiboot2.c \
               $(ARCH_DIR)/vga.c $(ARCH_DIR)/serial_driver.c $(ARCH_DIR)/apic.c \
               $(ARCH_DIR)/acpi.c $(ARCH_DIR)/idt.c $(ARCH_DIR)/timer.c $(ARCH_DIR)/rtc.c \
               $(ARCH_DIR)/ipi.c $(ARCH_DIR)/power.c $(ARCH_DIR)/syscall.c \
               $(ARCH_DIR)/uaccess.c

# AP Trampoline (assembled as part of kernel, uses PIC)
TRAMPOLINE_SRC := $(ARCH_DIR)/ap_trampoline.S
TRAMPOLINE_OBJ := $(BUILD_DIR)/ap_trampoline.o

# Multiboot2 header (assembled as object file for linking)
MULTIBOOT_HEADER_SRC := $(ARCH_DIR)/multiboot_header.S
MULTIBOOT_HEADER_OBJ := $(BUILD_DIR)/multiboot_header.o


# Architecture-independent kernel sources
KERNEL_DIR := kernel
KERNEL_C_SRCS := $(KERNEL_DIR)/device.c $(KERNEL_DIR)/pmm.c $(KERNEL_DIR)/pcd.c \
                 $(KERNEL_DIR)/slab.c $(KERNEL_DIR)/test.c $(KERNEL_DIR)/klog.c \
                 $(KERNEL_DIR)/monitor/monitor.c \
                 $(KERNEL_DIR)/thread.c \
                 $(KERNEL_DIR)/scheduler.c \
                 $(KERNEL_DIR)/vm.c \
                 $(KERNEL_DIR)/process.c \
                 $(KERNEL_DIR)/kmap.c

# Minilibc sources
MINILIBC_C_SRCS := lib/minilibc/string.c \
                   lib/minilibc/printf.c
MINILIBC_OBJS := $(patsubst lib/minilibc/%.c,$(BUILD_DIR)/minilibc_%.o,$(MINILIBC_C_SRCS))

# Include test build configuration (defines TESTS_OBJS)
include tests/build.mk

# All C sources
C_SRCS := $(ARCH_C_SRCS) $(KERNEL_C_SRCS)

# Objects
ARCH_BOOT_OBJ := $(patsubst $(ARCH_DIR)/%.S,$(BUILD_DIR)/boot_%.o,$(ARCH_DIR)/boot.S) \
                $(patsubst $(ARCH_DIR)/%.S,$(BUILD_DIR)/isr_%.o,$(ARCH_DIR)/isr.S) \
                $(BUILD_DIR)/boot_monitor_monitor_call.o \
                $(BUILD_DIR)/boot_syscall_entry.o \
                $(BUILD_DIR)/boot_userprog.o \
                $(BUILD_DIR)/boot_context.o
ARCH_OBJS := $(patsubst $(ARCH_DIR)/%.c,$(BUILD_DIR)/arch_%.o,$(ARCH_C_SRCS))
KERNEL_OBJS := $(patsubst $(KERNEL_DIR)/%.c,$(BUILD_DIR)/kernel_%.o,$(KERNEL_C_SRCS))

# Generated command line object (embedded fallback)
CMDLINE_OBJ := $(BUILD_DIR)/kernel_cmdline_source.o

# Add cmdline object to OBJS (TESTS_OBJS now includes conditionally compiled tests)
OBJS := $(ARCH_BOOT_OBJ) $(ARCH_OBJS) $(KERNEL_OBJS) $(MINILIBC_OBJS) $(TESTS_OBJS) $(TRAMPOLINE_OBJ) $(CMDLINE_OBJ)

KERNEL_ELF := $(BUILD_DIR)/$(KERNEL).elf
KERNEL_BIN := $(BUILD_DIR)/$(KERNEL).bin

.PHONY: all clean run run-debug help

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
	@echo "Run targets (using test framework):"
	@echo "  run              - Run kernel in QEMU (4 CPUs, 128M RAM, 8s timeout)"
	@echo "  run-debug        - Run kernel in QEMU with GDB server on port 1234"
	@echo ""
	@echo "Test targets (see tests/Makefile):"
	@echo "  tests            - Run all enabled tests"
	@echo "  tests-boot       - Basic kernel boot test (1 CPU)"
	@echo "  tests-apic-timer - APIC timer interrupt test"
	@echo "  tests-smp        - SMP boot test (2 CPUs)"
	@echo "  tests-pcd        - Page Control Data test"
	@echo "  tests-slab       - Slab allocator test"
	@echo "  tests-sched      - Thread creation and FIFO scheduling test"
	@echo "  tests-minilibc   - Minilibc string library test"
	@echo "  tests-usermode   - User mode syscall test (KVM enabled)"
	@echo "  tests-multiboot  - Multiboot2 header test"
	@echo ""
	@echo "Nested Kernel tests (tests-nk-*):"
	@echo "  tests-nk                        - Run all NK tests"
	@echo "  tests-nk-invariants             - NK invariants (ASPLOS '15)"
	@echo "  tests-nk-fault-injection        - NK fault injection (destructive)"
	@echo "  tests-nk-readonly-visibility    - Read-only mapping visibility"
	@echo "  tests-nk-smp-monitor-stress     - SMP monitor stress test"
	@echo ""
	@echo "Build options (override kernel.config):"
	@echo "  make CONFIG_TESTS_SPINLOCK=1              - Enable spinlock tests"
	@echo "  make CONFIG_TESTS_PMM=1                   - Enable PMM tests"
	@echo "  make CONFIG_TESTS_SLAB=1                  - Enable slab allocator tests"
	@echo "  make CONFIG_TESTS_APIC_TIMER=1            - Enable APIC timer test"
	@echo "  make CONFIG_TESTS_BOOT=1                  - Enable boot tests"
	@echo "  make CONFIG_TESTS_SMP=1                   - Enable SMP tests"
	@echo "  make CONFIG_TESTS_PCD=1                   - Enable PCD tests"
	@echo "  make CONFIG_TESTS_MINILIBC=1              - Enable minilibc tests"
	@echo "  make CONFIG_TESTS_USERMODE=1              - Enable user mode syscall tests"
	@echo "  make CONFIG_TESTS_NK_INVARIANTS=1         - Enable NK invariants tests"
	@echo "  make CONFIG_TESTS_NK_READONLY_VISIBILITY=1- Enable NK read-only visibility tests"
	@echo "  make CONFIG_TESTS_NK_FAULT_INJECTION=1    - Enable NK fault injection tests"
	@echo "  make CONFIG_TESTS_NK_TRAMPOLINE=1         - Enable NK trampoline test"
	@echo "  make CONFIG_TESTS_NK_INVARIANTS_VERIFY=1  - Verify NK invariants write protection"
	@echo ""
	@echo "Debug options:"
	@echo "  make CONFIG_DEBUG_SMP_AP=1                - Enable SMP AP debug marks"
	@echo "  make CONFIG_DEBUG_PCD_STATS=1             - Show PCD statistics"
	@echo "  make CONFIG_DEBUG_NK_INVARIANTS_VERBOSE=1 - Verbose NK invariants output"

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

# Compile context switch assembly
$(BUILD_DIR)/boot_context.o: $(ARCH_DIR)/context.S | $(BUILD_DIR)
	@echo "  AS      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

# Compile architecture-specific C files
$(BUILD_DIR)/arch_%.o: $(ARCH_DIR)/%.c $(CONFIG_DEP) | $(BUILD_DIR)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

# Compile architecture-independent kernel C files
$(BUILD_DIR)/kernel_%.o: $(KERNEL_DIR)/%.c $(CONFIG_DEP) | $(BUILD_DIR)
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel_monitor/%.o: $(KERNEL_DIR)/monitor/%.c $(CONFIG_DEP) | $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)/kernel_monitor
	@echo "  CC      $<"
	$(Q)$(CC) $(CFLAGS) -c $< -o $@

# Compile minilibc C files
$(BUILD_DIR)/minilibc_%.o: lib/minilibc/%.c $(CONFIG_DEP) | $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)
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

# Assemble multiboot header as 64-bit object file (for linking at offset 0)
$(MULTIBOOT_HEADER_OBJ): $(MULTIBOOT_HEADER_SRC) | $(BUILD_DIR)
	@echo "  AS      $<"
	$(Q)$(AS) --64 -o $@ $<

# Link kernel (trampoline is included as regular object)
$(KERNEL_ELF): $(MULTIBOOT_HEADER_OBJ) $(OBJS)
	@echo "  LD      $@"
	$(Q)$(LD) $(LDFLAGS) -T $(ARCH_LINKER) $^ -o $@

# Extract raw binary from ELF (for booting)
$(KERNEL_BIN): $(KERNEL_ELF)
	objcopy -O binary $< $@

# Create ISO (use ELF for multiboot2)
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
	PYTHONUNBUFFERED=1 python3 tests/run.py --timeout 8 --cpus 4 || exit 0

run-debug: $(ISO)
	PYTHONUNBUFFERED=1 python3 tests/run.py --debug || exit 0

clean:
	rm -rf $(BUILD_DIR) $(ISO_DIR) $(ISO)
	rm -f ./emergence_test_* 2>/dev/null || true
	rm -f /tmp/emergence_* 2>/dev/null || true
	rm -f *.bin *.elf 2>/dev/null || true
	find . -type d -name '__pycache__' -exec rm -rf {} + 2>/dev/null || true
	find . -type f -name '*.pyc' -delete 2>/dev/null || true
	find . -type f -name '*.pyo' -delete 2>/dev/null || true
	rm -rf tests/test_results 2>/dev/null || true

# Include test targets from tests/Makefile
include tests/Makefile



