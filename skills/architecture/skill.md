---
name: architecture
description: Use when designing and planning new features.
---

# SMP support

1. The parallel shared critical sections must use lock protection.
2. All requirements should be workable SMP configuration.

# Code Organization

- Platform drivers: `arch/${ARCH}/drivers/`
- Common utilities: `kernel/` or `lib/`
- Public headers: `include/`
