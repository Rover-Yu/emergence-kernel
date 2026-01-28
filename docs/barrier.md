# Memory Barrier API

This document describes the Emergence Kernel memory barrier API, which provides control over memory ordering in multi-processor environments.

## Overview

Memory barriers (also called memory fences) prevent CPU and compiler reordering of memory operations. In modern CPUs, memory operations can be reordered for performance; barriers enforce ordering guarantees for synchronization.

## Header

```c
#include <barrier.h>
```

## Why Memory Barriers Matter

Without barriers, CPUs and compilers may reorder operations:

```c
// Thread 1
x = 1;
ready = true;

// Thread 2
if (ready) {
    print(x);  // Could print 0 without barriers!
}
```

The CPU may reorder the stores in Thread 1, or the loads in Thread 2, causing Thread 2 to see `ready == true` but `x == 0`.

## API Reference

### Compiler Barrier

```c
#define barrier() __asm__ __volatile__("" ::: "memory")
```

Prevents the compiler from reordering memory operations across the barrier. Does **not** emit CPU instructions.

**Effects:**
- Forces all memory values to be flushed from registers
- Prevents compiler reordering across the barrier
- No CPU barrier instructions emitted (zero runtime cost)

**Use Cases:**
- Signal handler synchronization (with `atomic_signal_fence`)
- Device drivers accessing memory-mapped I/O
- Preventing compiler-only reordering

### Full Memory Barrier

```c
#define smp_mb() __asm__ __volatile__("mfence" ::: "memory")
```

Emits an `MFENCE` instruction on x86_64. Prevents all reordering of loads and stores.

**Guarantees:**
- All loads before `smp_mb()` complete before any loads after
- All stores before `smp_mb()` complete before any stores after
- All loads before complete before any stores after

**Example: Sequential Consistency**

```c
// Thread 1
data->value = 42;
smp_mb();
data->ready = 1;

// Thread 2
if (atomic_load(&data->ready)) {
    smp_mb();
    print(data->value);  // Guaranteed to see complete write
}
```

### Read Memory Barrier

```c
#define smp_rmb() barrier()
```

On x86_64, this is just a compiler barrier. x86_64 has strong read ordering (loads are never reordered with other loads).

**Guarantees:**
- All loads before complete before any loads after
- On x86_64: compiler-only barrier (no instruction emitted)

### Write Memory Barrier

```c
#define smp_wmb() barrier()
```

On x86_64, this is just a compiler barrier. x86_64 has strong store ordering (stores to different locations are never reordered).

**Guarantees:**
- All stores before complete before any stores after
- On x86_64: compiler-only barrier (no instruction emitted)

**Example: Buffer Publishing**

```c
// Producer
struct buffer *buf = allocate_buffer();
fill_buffer(buf);
smp_wmb();  // Ensure buffer is complete before publishing
atomic_store(&producer->buf, buf);

// Consumer
struct buffer *buf = atomic_load(&producer->buf);
if (buf) {
    smp_rmb();  // Ensure we see complete buffer
    process_buffer(buf);
}
```

### Dependency Barrier

```c
#define smp_read_barrier_depends() barrier()
```

On x86_64, this is a compiler barrier. Some architectures (e.g., Alpha) require special handling for data-dependent loads.

**Use Case**: Pointer-chasing data structures where the loaded address affects subsequent loads.

### Load-Acquire

```c
#define smp_load_acquire(ptr) \
    ({ \
        typeof(*ptr) ___val = __atomic_load_n(ptr, memory_order_acquire); \
        barrier(); \
        ___val; \
    })
```

Loads a value with acquire semantics. No memory operations after this point may be reordered before the load.

**Guarantees:**
- Acts as a read barrier for operations following the load
- Pairs with `smp_store_release()`

**Example: Lock-Free Flag Reading**

```c
struct data {
    atomic_int ready;
    int value;
};

// Producer
data->value = 42;
smp_store_release(&data->ready, 1);

// Consumer
if (smp_load_acquire(&data->ready)) {
    print(data->value);  // Guaranteed to see 42
}
```

### Store-Release

```c
#define smp_store_release(ptr, val) \
    do { \
        barrier(); \
        __atomic_store_n(ptr, val, memory_order_release); \
    } while (0)
```

Stores a value with release semantics. No memory operations before this point may be reordered after the store.

**Guarantees:**
- Acts as a write barrier for operations preceding the store
- Pairs with `smp_load_acquire()`

**Example: Spin Lock Release**

```c
void spin_unlock(atomic_flag *lock) {
    smp_store_release(lock, 0);  // Ensure critical section completes
}
```

### CPU Relax

```c
#define cpu_relax() __asm__ __volatile__("pause")
#define cpu_pause() cpu_relax()
```

Emits the `PAUSE` instruction on x86_64. This is NOT a memory barrier; it hints to the CPU that the code is in a spin-wait loop.

**Benefits:**
- Reduces power consumption during spin waits
- Improves hyper-threading performance
- Prevents memory pipeline bottlenecks

**Example: Spin Lock with Backoff**

```c
void spin_lock(atomic_flag *lock) {
    while (atomic_flag_test_and_set(lock)) {
        cpu_relax();  // Hint to CPU that we're spinning
    }
}
```

### Volatile Access Helpers

```c
#define READ_ONCE(val) \
    ({ \
        typeof(val) ___val = (val); \
        barrier(); \
        ___val; \
    })

#define WRITE_ONCE(ptr, val) \
    do { \
        barrier(); \
        *(volatile typeof(ptr) *)(ptr) = (val); \
    } while (0)
```

Access a value exactly once, preventing compiler optimizations that may load/store multiple times.

**Problem Solved:**

```c
// WITHOUT READ_ONCE:
if (x == 0)  // First load
    print(x);  // Second load (compiler may reload!)

// WITH READ_ONCE:
if (READ_ONCE(x) == 0)
    print(READ_ONCE(x));  // Same access semantics
```

**Use Cases:**
- Accessing shared variables in loops
- Data races in lockless algorithms
- Interacting with hardware registers
- KASAN (sanitizer) compatibility

## Barrier Selection Guide

| Situation | Recommended Barrier |
|-----------|---------------------|
| Publishing data after writing | `smp_store_release()` |
| Reading data after flag check | `smp_load_acquire()` |
| Full synchronization | `smp_mb()` |
| Preventing compiler reordering only | `barrier()` |
| Spin-wait loop | `cpu_relax()` |
| Accessing shared variable in loop | `READ_ONCE()` / `WRITE_ONCE()` |

## x86_64 Memory Ordering

x86_64 has **strong memory ordering** (TSO - Total Store Order), which simplifies barriers:

| Barrier | x86_64 Implementation |
|---------|----------------------|
| `smp_mb()` | `MFENCE` instruction |
| `smp_rmb()` | Compiler barrier only |
| `smp_wmb()` | Compiler barrier only |
| `smp_load_acquire()` | Compiler barrier + load |
| `smp_store_release()` | Store + compiler barrier |

**Key x86_64 Guarantees:**
- Loads are never reordered with other loads
- Stores are never reordered with other stores
- Loads are never reordered with older stores
- Stores may be reordered with older loads

## Common Patterns

### Producer-Consumer (Acquire/Release)

```c
// Producer
data->value = compute();
smp_store_release(&data->ready, 1);

// Consumer
if (smp_load_acquire(&data->ready)) {
    use(data->value);
}
```

### Reference Counting

```c
void get_ref(struct obj *obj) {
    atomic_fetch_add(&obj->refcount, 1);
    smp_mb();  // Ensure object exists before use
}

void put_ref(struct obj *obj) {
    smp_mb();  // Ensure access completes before decrement
    if (atomic_fetch_sub(&obj->refcount, 1) == 1) {
        free(obj);
    }
}
```

### Lock-Free Stack (Example)

```c
struct node *stack_pop(atomic_uintptr_t *top) {
    struct node *old_top, *new_top;

    do {
        old_top = (struct node *)smp_load_acquire(top);
        if (!old_top) return NULL;
        new_top = old_top->next;
    } while (!atomic_compare_exchange_weak(top,
        (uintptr_t *)&old_top, (uintptr_t)new_top));

    return old_top;
}
```

## Performance Impact

| Barrier | Approx Cost (cycles) |
|---------|---------------------|
| `barrier()` (compiler only) | 0 |
| `cpu_relax()` | Negligible |
| `smp_rmb()` / `smp_wmb()` (x86_64) | 0 (compiler only) |
| `smp_mb()` / `mfence` | 10-50 |
| `smp_load_acquire` / `smp_store_release` | Similar to regular load/store |

**Optimization Tip**: Use acquire/release instead of full barriers when possible.

## See Also

- `docs/atomic.md` - Atomic operations API
- `include/atomic.h` - Memory order enum definitions
- `arch/x86_64/spinlock_arch.h` - Spin lock implementations
