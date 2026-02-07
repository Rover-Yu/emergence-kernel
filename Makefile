# Emergence Kernel Makefile

KERNEL := emergence
ISO := emergence.iso
BUILD_DIR := build
ISO_DIR := isodir

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

# Flags (x86_64 with multiboot support)
CFLAGS := -ffreestanding -O2 -Wall -g -nostdlib -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -I.
CFLAGS += -DCONFIG_SPINLOCK_TESTS=$(CONFIG_SPINLOCK_TESTS)
CFLAGS += -DCONFIG_PMM_TESTS=$(CONFIG_PMM_TESTS)
CFLAGS += -DCONFIG_SMP_AP_DEBUG=$(CONFIG_SMP_AP_DEBUG)
CFLAGS += -DCONFIG_APIC_TIMER_TEST=$(CONFIG_APIC_TIMER_TEST)
CFLAGS += -DCONFIG_WRITE_PROTECTION_VERIFY=$(CONFIG_WRITE_PROTECTION_VERIFY)
CFLAGS += -DCONFIG_INVARIANTS_VERBOSE=$(CONFIG_INVARIANTS_VERBOSE)
CFLAGS += -DCONFIG_PCD_STATS=$(CONFIG_PCD_STATS)
CFLAGS += -DCONFIG_NK_PROTECTION_TESTS=$(CONFIG_NK_PROTECTION_TESTS)
CFLAGS += -DCONFIG_USERMODE_TEST=$(CONFIG_USERMODE_TEST)
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
                 $(KERNEL_DIR)/monitor/monitor.c

# Spinlock test sources (conditionally compiled)
SPINLOCK_TEST_SRC := tests/spinlock/spinlock_test.c
SPINLOCK_TEST_OBJ := $(BUILD_DIR)/kernel_spinlock_test.o

# Nested kernel mappings protection test sources (conditionally compiled)
NK_PROTECTION_TEST_SRC := tests/nested_kernel_mapping_protection/nk_protection_test.c
NK_PROTECTION_TEST_OBJ := $(BUILD_DIR)/nk_protection_test.o

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
TESTS_OBJS := $(patsubst $(TESTS_DIR)/%.c,$(BUILD_DIR)/test_%.o,$(TESTS_C_SRCS))

# Conditionally include test objects
ifeq ($(CONFIG_SPINLOCK_TESTS),1)
TESTS_OBJS += $(SPINLOCK_TEST_OBJ)
endif
ifeq ($(CONFIG_NK_PROTECTION_TESTS),1)
TESTS_OBJS += $(NK_PROTECTION_TEST_OBJ)
endif

OBJS := $(ARCH_BOOT_OBJ) $(ARCH_OBJS) $(KERNEL_OBJS) $(TESTS_OBJS) $(TRAMPOLINE_OBJ)
KERNEL_ELF := $(BUILD_DIR)/$(KERNEL).elf

.PHONY: all clean run run-debug test test-all test-boot test-apic-timer test-smp test-pcd test-nested-kernel test-nk-protection test-readonly-visibility help

all: $(ISO)

help:
	@echo "Emergence Kernel Make Targets"
	@echo "=============================="
	@echo ""
	@echo "Build targets:"
	@echo "  all              - Build kernel ISO"
	@echo "  clean            - Remove build artifacts"
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
	@echo "  test-nested-kernel     - Nested Kernel invariants test"
	@echo "  test-nk-protection     - Nested Kernel mappings protection test"
	@echo "  test-readonly-visibility - Read-only visibility test"
	@echo ""
	@echo "Build options (override kernel.config):"
	@echo "  make CONFIG_SPINLOCK_TESTS=1           - Enable spinlock tests"
	@echo "  make CONFIG_PMM_TESTS=1                - Enable PMM tests"
	@echo "  make CONFIG_SMP_AP_DEBUG=1             - Enable AP debug marks"
	@echo "  make CONFIG_APIC_TIMER_TEST=1          - Enable APIC timer test"
	@echo "  make CONFIG_WRITE_PROTECTION_VERIFY=1  - Verify write protection"
	@echo "  make CONFIG_INVARIANTS_VERBOSE=1       - Verbose invariants output"
	@echo "  make CONFIG_PCD_STATS=1                - Show PCD statistics"
	@echo "  make CONFIG_NK_PROTECTION_TESTS=1      - Enable NK protection tests"

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(ISO_DIR):
	mkdir -p $(ISO_DIR)/boot/grub

# Compile architecture-specific boot assembly
$(BUILD_DIR)/boot_%.o: $(ARCH_DIR)/boot.S | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile ISR assembly
$(BUILD_DIR)/isr_%.o: $(ARCH_DIR)/isr.S | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile monitor assembly files
$(BUILD_DIR)/boot_monitor_%.o: $(ARCH_DIR)/monitor/%.S | $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@


# Compile syscall_entry assembly
$(BUILD_DIR)/boot_syscall_entry.o: $(ARCH_DIR)/syscall_entry.S | $(BUILD_DIR)
	$$(CC) $$(CFLAGS) -c $$< -o $$@

# Compile userprog assembly
# Compile userprog assembly
$(BUILD_DIR)/boot_userprog.o: $(ARCH_DIR)/userprog.S | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile architecture-specific C files
$(BUILD_DIR)/arch_%.o: $(ARCH_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile architecture-independent kernel C files
$(BUILD_DIR)/kernel_%.o: $(KERNEL_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel_monitor/%.o: $(KERNEL_DIR)/monitor/%.c | $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)/kernel_monitor
	$(CC) $(CFLAGS) -c $< -o $@

# Compile spinlock test (from tests/spinlock/) - only if enabled
ifeq ($(CONFIG_SPINLOCK_TESTS),1)
$(SPINLOCK_TEST_OBJ): $(SPINLOCK_TEST_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@
endif

# Compile nested kernel mappings protection test (from tests/nested_kernel_mapping_protection/) - only if enabled
ifeq ($(CONFIG_NK_PROTECTION_TESTS),1)
$(NK_PROTECTION_TEST_OBJ): $(NK_PROTECTION_TEST_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@
endif

# Compile test C files
$(BUILD_DIR)/test_%.o: $(TESTS_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile AP trampoline (as 64-bit assembly, uses PIC)
# Use CC to get C preprocessor for conditional compilation
$(TRAMPOLINE_OBJ): $(TRAMPOLINE_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Link kernel (trampoline is included as regular object)
$(KERNEL_ELF): $(OBJS)
	$(LD) $(LDFLAGS) -T $(ARCH_LINKER) $^ -o $@

# Create ISO
$(ISO): $(KERNEL_ELF) | $(ISO_DIR)
	cp $(KERNEL_ELF) $(ISO_DIR)/boot/$(KERNEL).elf
	echo 'set timeout=0' > $(ISO_DIR)/boot/grub/grub.cfg
	echo 'set default=0' >> $(ISO_DIR)/boot/grub/grub.cfg
	echo 'menuentry "Emergence Kernel" {' >> $(ISO_DIR)/boot/grub/grub.cfg
	echo '    multiboot2 /boot/$(KERNEL).elf' >> $(ISO_DIR)/boot/grub/grub.cfg
	echo '    boot' >> $(ISO_DIR)/boot/grub/grub.cfg
	echo '}' >> $(ISO_DIR)/boot/grub/grub.cfg
	env TMPDIR=$(PWD)/.tmp $(GRUB_MKRESCUE) -o $@ $(ISO_DIR)

run: $(ISO)
	qemu-system-x86_64 -enable-kvm -M pc -m 128M -nographic -cdrom $(ISO) -smp 4 -device isa-debug-exit,iobase=0xB004,iosize=1 || exit 0

run-debug: $(ISO)
	qemu-system-x86_64 -enable-kvm -M pc -m 128M -nographic -cdrom $(ISO) -smp 4 -s -S -device isa-debug-exit,iobase=0xB004,iosize=1 || exit 0

clean:
	rm -rf $(BUILD_DIR) $(ISO_DIR) $(ISO)

# Test targets
test: test-all

test-all:
	@echo "Running Emergence Kernel test suite..."
	@cd tests && ./run_all_tests.sh

test-boot:
	@echo "Running Basic Kernel Boot Test..."
	@cd tests && ./boot/boot_test.sh

test-apic-timer:
	@echo "Running APIC Timer Test..."
	@cd tests && ./timer/apic_timer_test.sh

test-smp:
	@echo "Running SMP Boot Test..."
	@cd tests && ./smp/smp_boot_test.sh 4

test-pcd:
	@echo "Running Page Control Data (PCD) Test..."
	@cd tests && ./pcd/pcd_test.sh

test-nested-kernel:
	@echo "Running Nested Kernel Invariants Test..."
	@cd tests && ./nested_kernel_invariants/nested_kernel_invariants_test.sh

test-nk-protection:
	@echo "Running Nested Kernel Mappings Protection Test..."
	@cd tests && ./nk_protection/nk_protection_test.sh

test-readonly-visibility:
	@echo "Running Read-Only Visibility Test..."
	@cd tests && ./readonly_visibility/readonly_visibility_test.sh

# Compile syscall_entry assembly
$(BUILD_DIR)/boot_syscall_entry.o: $(ARCH_DIR)/syscall_entry.S | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@
