# JAKernel Makefile

KERNEL := jakernel
ISO := jakernel.iso
BUILD_DIR := build
ISO_DIR := isodir

# Tools
CC := gcc
AS := as
LD := ld
GRUB_MKRESCUE := grub-mkrescue

# Flags (x86_64 with multiboot support)
CFLAGS := -ffreestanding -O2 -Wall -Wextra -nostdlib -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -I.
LDFLAGS := -nostdlib -m elf_x86_64

# Architecture-specific sources (x86_64)
ARCH_DIR := arch/x86_64
ARCH_BOOT_SRC := $(ARCH_DIR)/boot.S $(ARCH_DIR)/isr.S
ARCH_LINKER := $(ARCH_DIR)/linker.ld
ARCH_C_SRCS := $(ARCH_DIR)/vga.c $(ARCH_DIR)/serial_driver.c $(ARCH_DIR)/apic.c $(ARCH_DIR)/acpi.c $(ARCH_DIR)/idt.c $(ARCH_DIR)/timer.c $(ARCH_DIR)/rtc.c $(ARCH_DIR)/ipi.c

# AP Trampoline (built as 16-bit binary, included via incbin)
TRAMPOLINE_SRC := ap_trampoline.bin.S
TRAMPOLINE_BIN := $(BUILD_DIR)/ap_trampoline.bin
TRAMPOLINE_OBJ := $(BUILD_DIR)/ap_trampoline.o

# Architecture-independent kernel sources
KERNEL_DIR := kernel
KERNEL_C_SRCS := $(KERNEL_DIR)/main.c $(KERNEL_DIR)/device.c $(KERNEL_DIR)/smp.c

# Test sources (reference only, not compiled into kernel)
# These test files are kept for documentation purposes
TESTS_DIR := tests
TESTS_C_SRCS :=  # Intentionally empty - test files are reference only

# All C sources
C_SRCS := $(ARCH_C_SRCS) $(KERNEL_C_SRCS)

# Objects
ARCH_BOOT_OBJ := $(patsubst $(ARCH_DIR)/%.S,$(BUILD_DIR)/boot_%.o,$(ARCH_DIR)/boot.S) \
                $(patsubst $(ARCH_DIR)/%.S,$(BUILD_DIR)/isr_%.o,$(ARCH_DIR)/isr.S)
ARCH_OBJS := $(patsubst $(ARCH_DIR)/%.c,$(BUILD_DIR)/arch_%.o,$(ARCH_C_SRCS))
KERNEL_OBJS := $(patsubst $(KERNEL_DIR)/%.c,$(BUILD_DIR)/kernel_%.o,$(KERNEL_C_SRCS))
TESTS_OBJS := $(patsubst $(TESTS_DIR)/%.c,$(BUILD_DIR)/test_%.o,$(TESTS_C_SRCS))
OBJS := $(ARCH_BOOT_OBJ) $(ARCH_OBJS) $(KERNEL_OBJS) $(TESTS_OBJS)
KERNEL_ELF := $(BUILD_DIR)/$(KERNEL).elf

.PHONY: all clean run

all: $(ISO)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(ISO_DIR):
	mkdir -p $(ISO_DIR)/boot/grub

# Compile architecture-specific boot assembly
$(BUILD_DIR)/boot_%.o: $(ARCH_DIR)/boot.S $(TRAMPOLINE_BIN) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile ISR assembly
$(BUILD_DIR)/isr_%.o: $(ARCH_DIR)/isr.S | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile architecture-specific C files
$(BUILD_DIR)/arch_%.o: $(ARCH_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile architecture-independent kernel C files
$(BUILD_DIR)/kernel_%.o: $(KERNEL_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile test C files
$(BUILD_DIR)/test_%.o: $(TESTS_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Build AP trampoline (as 16-bit binary)
$(TRAMPOLINE_BIN): $(TRAMPOLINE_SRC) ap_trampoline.ld | $(BUILD_DIR)
	as --32 -o $(BUILD_DIR)/ap_trampoline.tmp.o $<
	ld -m elf_i386 -T ap_trampoline.ld -o $(BUILD_DIR)/ap_trampoline.elf $(BUILD_DIR)/ap_trampoline.tmp.o
	objcopy -O binary $(BUILD_DIR)/ap_trampoline.elf $@
	rm $(BUILD_DIR)/ap_trampoline.tmp.o $(BUILD_DIR)/ap_trampoline.elf

# Convert trampoline binary to object file for _binary_ symbols
$(TRAMPOLINE_OBJ): $(TRAMPOLINE_BIN)
	objcopy -I binary -O elf64-x86-64 -B i386 --rename-section .data=.ap_trampoline_incbin $< $@

# Link (trampoline object included for _binary_ symbols)
$(KERNEL_ELF): $(OBJS) $(TRAMPOLINE_OBJ)
	$(LD) $(LDFLAGS) -T $(ARCH_LINKER) $^ -o $@

# Create ISO
$(ISO): $(KERNEL_ELF) | $(ISO_DIR)
	cp $(KERNEL_ELF) $(ISO_DIR)/boot/$(KERNEL).elf
	echo 'set timeout=0' > $(ISO_DIR)/boot/grub/grub.cfg
	echo 'set default=0' >> $(ISO_DIR)/boot/grub/grub.cfg
	echo 'menuentry "JAKernel" {' >> $(ISO_DIR)/boot/grub/grub.cfg
	echo '    multiboot2 /boot/$(KERNEL).elf' >> $(ISO_DIR)/boot/grub/grub.cfg
	echo '    boot' >> $(ISO_DIR)/boot/grub/grub.cfg
	echo '}' >> $(ISO_DIR)/boot/grub/grub.cfg
	$(GRUB_MKRESCUE) -o $@ $(ISO_DIR)

run: $(ISO)
	qemu-system-x86_64 -M q35 -m 128M -serial stdio -cdrom $(ISO) -smp 4

run-debug: $(ISO)
	qemu-system-x86_64 -M pc -m 128M -serial stdio -cdrom $(ISO) -smp 4 -s -S

clean:
	rm -rf $(BUILD_DIR) $(ISO_DIR) $(ISO)
