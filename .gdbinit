# GDB initialization script for Emergence Kernel debugging

# Connect to QEMU's remote debugger
target remote :1234

# Load kernel symbols
symbol-file build/emergence.elf

# Set breakpoints
break kernel_main

# Continue execution
continue
