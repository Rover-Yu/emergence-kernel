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
CFLAGS := -ffreestanding -O2 -Wall -Wextra -nostdlib -mcmodel=large -mno-red-zone -mno-mmx -mno-sse -mno-sse2
LDFLAGS := -nostdlib -m elf_x86_64

# Sources
SRC_DIR := src
ASM_SRC := $(SRC_DIR)/boot.S
C_SRC := $(SRC_DIR)/kernel.c

# Objects
ASM_OBJ := $(BUILD_DIR)/boot.o
C_OBJ := $(BUILD_DIR)/kernel.o
KERNEL_ELF := $(BUILD_DIR)/$(KERNEL).elf

.PHONY: all clean run

all: $(ISO)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(ISO_DIR):
	mkdir -p $(ISO_DIR)/boot/grub

# Compile assembly with 64-bit support
$(ASM_OBJ): $(ASM_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile C
$(C_OBJ): $(C_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Link
$(KERNEL_ELF): $(ASM_OBJ) $(C_OBJ)
	$(LD) $(LDFLAGS) -T $(SRC_DIR)/linker.ld $^ -o $@

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
	qemu-system-x86_64 -M q35 -m 128M -serial stdio -cdrom $(ISO)

run-debug: $(ISO)
	qemu-system-x86_64 -M q35 -m 128M -serial stdio -cdrom $(ISO) -s -S

clean:
	rm -rf $(BUILD_DIR) $(ISO_DIR) $(ISO)
