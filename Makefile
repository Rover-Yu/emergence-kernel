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
ARCH_BOOT_SRC := $(ARCH_DIR)/boot.S
ARCH_LINKER := $(ARCH_DIR)/linker.ld
ARCH_C_SRCS := $(ARCH_DIR)/vga.c $(ARCH_DIR)/serial_driver.c $(ARCH_DIR)/apic.c $(ARCH_DIR)/acpi.c

# Architecture-independent kernel sources
KERNEL_DIR := kernel
KERNEL_C_SRCS := $(KERNEL_DIR)/main.c $(KERNEL_DIR)/device.c $(KERNEL_DIR)/smp.c

# All C sources
C_SRCS := $(ARCH_C_SRCS) $(KERNEL_C_SRCS)

# Objects
ARCH_BOOT_OBJ := $(BUILD_DIR)/boot.o
ARCH_OBJS := $(patsubst $(ARCH_DIR)/%.c,$(BUILD_DIR)/arch_%.o,$(ARCH_C_SRCS))
KERNEL_OBJS := $(patsubst $(KERNEL_DIR)/%.c,$(BUILD_DIR)/kernel_%.o,$(KERNEL_C_SRCS))
OBJS := $(ARCH_BOOT_OBJ) $(ARCH_OBJS) $(KERNEL_OBJS)
KERNEL_ELF := $(BUILD_DIR)/$(KERNEL).elf

.PHONY: all clean run

all: $(ISO)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(ISO_DIR):
	mkdir -p $(ISO_DIR)/boot/grub

# Compile architecture-specific boot assembly
$(ARCH_BOOT_OBJ): $(ARCH_BOOT_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile architecture-specific C files
$(BUILD_DIR)/arch_%.o: $(ARCH_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile architecture-independent kernel C files
$(BUILD_DIR)/kernel_%.o: $(KERNEL_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Link
$(KERNEL_ELF): $(OBJS)
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
	qemu-system-x86_64 -M q35 -m 128M -serial stdio -cdrom $(ISO) -smp 4 -s -S

clean:
	rm -rf $(BUILD_DIR) $(ISO_DIR) $(ISO)
