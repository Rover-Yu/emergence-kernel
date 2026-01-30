---
name: architecture
description: Use when designing and planning new features.
---

# Common Feature Requirements

1. Support SMP - shared critical sections must use lock protection.
2. Architecture-specific code goes in `arch/${ARCH}/`, headers in `arch/${ARCH}/include/`.
3. Architecture-independent code goes in `kernel/${SUBSYSTEM}`, headers in `include/` directory.

# Code Organization

- Platform drivers: `arch/${ARCH}/drivers/`
- Common utilities: `kernel/` or `lib/`
- Public headers: `include/`
