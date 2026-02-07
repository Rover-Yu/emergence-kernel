#!/bin/sh

# Run QEMU with GDB server enabled for debugging
# Port 1234 is used for GDB remote connection

qemu-system-x86_64 -M pc -m 1G -nographic -smp 1 \
	-cdrom ./emergence.iso \
	-s -S \
	-gdb tcp::1234
