#!/bin/sh

qemu-system-x86_64 -enable-kvm -M pc -m 128M -nographic -smp 4 \
	-cdrom ./jakernel.iso -device isa-debug-exit,iobase=0xB004,iosize=1 || exit 0
