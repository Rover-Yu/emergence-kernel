#!/bin/sh

qemu-system-x86_64 -M pc -m 128M -nographic -smp 4 \
	-cdrom ./jakernel.iso
