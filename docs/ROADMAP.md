# Emergence Kernel Roadmap

This document outlines the planned development milestones and future direction of the Emergence Kernel project.

## Project Vision

Emergence Kernel aims to become a research-oriented operating system kernel that combines traditional kernel design with modern programmable capabilities and innovative storage approaches. The roadmap focuses on building a solid foundation while introducing novel concepts in kernel programmability, file system design, and AI integration.

## Project Goals

The following goals represent the strategic direction of the Emergence Kernel. These goals are not prioritized by time order and can be pursued independently or in parallel based on contributor interest and expertise.

### Goal 1: Basic Kernel Capabilities

Implement essential kernel capabilities for task management and system operation.

**Deliverables:**
- User and kernel multitasking support
- Virtual memory management
- Complete interrupt handling framework
- Basic system call interface

**Note:** This goal focuses on core kernel functionality without building a complete drivers framework. The existing device driver framework will serve as the foundation for future expansion.

### Goal 2: Lua Programmable Kernel Framework

Integrate Lua as the kernel's scripting and extension language.

**Deliverables:**
- Lua runtime integration into kernel space
- Lua API for kernel operations (device management, memory allocation, process control)
- Safe sandboxing environment for kernel extensions
- Documentation for Lua kernel programming

**Rationale:** A programmable kernel enables rapid prototyping, runtime customization, and experimental exploration of kernel concepts without recompilation.

### Goal 3: Graph-Based Virtual File System

Design and implement a graph-based architecture as the core of the VFS layer.

**Deliverables:**
- Graph data structure for file system representation
- VFS interface supporting graph traversal and manipulation
- Integration with existing slab allocator for node management
- Example file system implementations using the graph interface

**Rationale:** Graph-based VFS provides flexible representation of complex file relationships and enables novel storage semantics.

### Goal 4: Automatic Memory Management

Implement automated memory management for kernel objects.

**Deliverables:**
- Reference counting or garbage collection for kernel objects
- Automatic cleanup of device drivers and file system nodes
- Memory leak detection and prevention
- Integration with existing slab allocator

**Rationale:** Automatic memory management reduces kernel bugs related to resource cleanup and simplifies driver development.

### Goal 5: LLM-Based File System Integration

Create or integrate an LLM-based open source file system (e.g., specfs).

**Deliverables:**
- Research and evaluation of LLM file system options
- Integration of selected LLM file system (specfs or alternative)
- API for semantic file operations
- Performance optimization and caching strategies

**Rationale:** LLM-based file systems enable semantic search, content-aware organization, and intelligent file operations.

### Goal 6: Nested Kernel Architecture

Adopt a nested kernel architecture for enhanced isolation and security.

**Deliverables:**
- Separation of privileged kernel (NK) and unprivileged kernel (OK) layers
- Memory isolation between kernel layers using CR0.WP toggle for page table protection
- Secure IPC mechanism for cross-layer communication via monitor calls
- Monitored system call path from unprivileged to privileged layer

**Design Decisions:**
- **CR0.WP Toggle:** Uses single shared page table with CR0.WP bit toggling instead of dual page tables with CR3 switching, eliminating TLB flushes on monitor calls
- **Page Table Focus:** Protection focuses on page table integrity (PTPs marked read-only in OK mode); privileged register integrity (CR0, CR3, CR4, etc.) is not enforced by the monitor
- **Simplified Model:** Prioritizes performance and simplicity over comprehensive hardware state protection

**Rationale:** Nested kernel architecture provides stronger isolation guarantees by enforcing memory protection even within kernel mode. The unprivileged kernel handles most operations while the privileged kernel validates sensitive operations, reducing the attack surface and containing bugs.

## Contributing

This roadmap represents the strategic direction of the project. Contributions that advance any milestone are welcome. Please refer to the project documentation for coding standards and submission guidelines.

---

*Last Updated: 2026-03-03*
