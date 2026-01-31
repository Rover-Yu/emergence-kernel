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
CFLAGS += -DCONFIG_CR0_WP_CONTROL=$(CONFIG_CR0_WP_CONTROL)
CFLAGS += -DCONFIG_INVARIANTS_VERBOSE=$(CONFIG_INVARIANTS_VERBOSE)
CFLAGS += -DCONFIG_PCD_STATS=$(CONFIG_PCD_STATS)
LDFLAGS := -nostdlib -m elf_x86_64

# Architecture-specific sources (x86_64)
ARCH_DIR := arch/x86_64
ARCH_BOOT_SRC := $(ARCH_DIR)/boot.S $(ARCH_DIR)/isr.S $(ARCH_DIR)/monitor/monitor_call.S
ARCH_LINKER := $(ARCH_DIR)/linker.ld
ARCH_C_SRCS := $(ARCH_DIR)/vga.c $(ARCH_DIR)/serial_driver.c $(ARCH_DIR)/apic.c $(ARCH_DIR)/acpi.c $(ARCH_DIR)/idt.c $(ARCH_DIR)/timer.c $(ARCH_DIR)/rtc.c $(ARCH_DIR)/ipi.c $(ARCH_DIR)/power.c

# AP Trampoline (assembled as part of kernel, uses PIC)
TRAMPOLINE_SRC := $(ARCH_DIR)/ap_trampoline.S
TRAMPOLINE_OBJ := $(BUILD_DIR)/ap_trampoline.o

# Architecture-independent kernel sources
KERNEL_DIR := kernel
KERNEL_C_SRCS := $(KERNEL_DIR)/main.c $(KERNEL_DIR)/device.c $(KERNEL_DIR)/smp.c \
                 $(KERNEL_DIR)/pmm.c $(KERNEL_DIR)/pcd.c $(KERNEL_DIR)/multiboot2.c \
                 $(KERNEL_DIR)/monitor/monitor.c

# Spinlock test sources (conditionally compiled)
SPINLOCK_TEST_SRC := tests/spinlock/spinlock_test.c
SPINLOCK_TEST_OBJ := $(BUILD_DIR)/kernel_spinlock_test.o

# Test sources (reference only, not compiled into kernel)
# These test files are kept for documentation purposes
TESTS_DIR := tests
TESTS_C_SRCS :=  # Intentionally empty - test files are reference only

# All C sources
C_SRCS := $(ARCH_C_SRCS) $(KERNEL_C_SRCS)

# Objects
ARCH_BOOT_OBJ := $(patsubst $(ARCH_DIR)/%.S,$(BUILD_DIR)/boot_%.o,$(ARCH_DIR)/boot.S) \
                $(patsubst $(ARCH_DIR)/%.S,$(BUILD_DIR)/isr_%.o,$(ARCH_DIR)/isr.S) \
                $(BUILD_DIR)/boot_monitor_monitor_call.o
ARCH_OBJS := $(patsubst $(ARCH_DIR)/%.c,$(BUILD_DIR)/arch_%.o,$(ARCH_C_SRCS))
KERNEL_OBJS := $(patsubst $(KERNEL_DIR)/%.c,$(BUILD_DIR)/kernel_%.o,$(KERNEL_C_SRCS))
TESTS_OBJS := $(patsubst $(TESTS_DIR)/%.c,$(BUILD_DIR)/test_%.o,$(TESTS_C_SRCS))

# Conditionally include spinlock test object
ifeq ($(CONFIG_SPINLOCK_TESTS),1)
OBJS := $(ARCH_BOOT_OBJ) $(ARCH_OBJS) $(KERNEL_OBJS) $(TESTS_OBJS) $(TRAMPOLINE_OBJ) $(SPINLOCK_TEST_OBJ)
else
OBJS := $(ARCH_BOOT_OBJ) $(ARCH_OBJS) $(KERNEL_OBJS) $(TESTS_OBJS) $(TRAMPOLINE_OBJ)
endif
KERNEL_ELF := $(BUILD_DIR)/$(KERNEL).elf

.PHONY: all clean run test test-all test-boot test-apic-timer test-smp test-pcd test-nested-kernel

all: $(ISO)

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
	$(GRUB_MKRESCUE) -o $@ $(ISO_DIR)

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
	@cd tests && ./smp/smp_boot_test.sh 2

test-pcd:
	@echo "Running Page Control Data (PCD) Test..."
	@cd tests && ./monitor/pcd_test.sh

test-nested-kernel:
	@echo "Running Nested Kernel Invariants Test..."
	@cd tests && ./monitor/nested_kernel_invariants_test.sh
