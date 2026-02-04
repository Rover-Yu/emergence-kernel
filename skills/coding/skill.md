---
name: coding
description: Requirements for source code
---

# Interface Design

1. Interface headers must exist in appropriate include directories.

# Coding Style

1. Use Linux kernel coding style.
2. Use C11 standard.
3. Only use common compiler extensions.

# Source Organization

1. Architecture-specific sources are in folder `arch/${ARCH}`.
2. Architecture-independent core sources are in folder `kernel`.
3. Tests are in folder `tests`,  A test case include two parts,
   e.g. spin lock tests include spinlock_test.c and spinlock_test.sh.
   a) spinlock_test.c : the test functions in kernel, write by C language.
   b) spinlock_test.sh : the script of lauch qemu VM and verify test results by check VM logs.
   c) these two files are in the folder tests/spinlock/.
