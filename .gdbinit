# GDB initialization script for JAKernel debugging

# Connect to QEMU's remote debugger
target remote :1234

# Load kernel symbols
symbol-file build/jakernel.elf

# Set breakpoints
break kernel_main

# Continue execution
continue
