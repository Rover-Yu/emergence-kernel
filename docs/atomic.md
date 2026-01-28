# Atomic Operations API

This document describes the Emergence Kernel atomic operations API, which provides thread-safe atomic operations for multi-processor synchronization.

## Overview

Atomic operations are indivisible operations that complete without interruption. In an SMP (Symmetric Multi-Processing) system, atomic operations prevent race conditions when multiple CPUs access shared memory concurrently.

## Header Files

| Header | Purpose |
|--------|---------|
| `include/atomic.h` | Generic C11-compatible atomic API |
| `arch/x86_64/atomic.h` | x86_64 optimized assembly implementations |

## Memory Ordering

All atomic operations support explicit memory ordering constraints via the `memory_order` enum:

```c
typedef enum {
    memory_order_relaxed,   // No ordering guarantees
    memory_order_consume,   // Data dependency ordering
    memory_order_acquire,   // Load-acquire (synchronizes-with release)
    memory_order_release,   // Store-release (synchronizes-with acquire)
    memory_order_acq_rel,   // Both acquire and release
    memory_order_seq_cst    // Sequentially consistent (default)
} memory_order;
```

### Memory Order Summary

| Order | Description | Use Case |
|-------|-------------|----------|
| `relaxed` | No synchronization | Counters, statistics |
| `consume` | Dependency ordering | Pointer-based data structures |
| `acquire` | Read barrier | Lock acquisition, reading flags |
| `release` | Write barrier | Lock release, publishing data |
| `acq_rel` | Read + write barrier | Read-modify-write operations |
| `seq_cst` | Global ordering | Default when unsure |

## Atomic Types

Standard atomic integer types:

```c
atomic_int              // volatile _Atomic int
atomic_uint             // volatile _Atomic unsigned int
atomic_long             // volatile _Atomic long
atomic_ulong            // volatile _Atomic unsigned long
atomic_intptr_t         // volatile _Atomic intptr_t
atomic_uintptr_t        // volatile _Atomic uintptr_t
atomic_bool             // volatile _Atomic _Bool
```

## API Reference

### Atomic Flag (Spin Lock Primitive)

The `atomic_flag` type provides the simplest lock-free primitive, guaranteed to be lock-free.

```c
typedef volatile _Atomic _Bool atomic_flag;
#define ATOMIC_FLAG_INIT false

// Test and set the flag (returns previous value)
bool atomic_flag_test_and_set(volatile atomic_flag *obj);
bool atomic_flag_test_and_set_explicit(volatile atomic_flag *obj, memory_order order);

// Clear the flag
void atomic_flag_clear(volatile atomic_flag *obj);
void atomic_flag_clear_explicit(volatile atomic_flag *obj, memory_order order);
```

**Example: Simple Spin Lock**

```c
atomic_flag lock = ATOMIC_FLAG_INIT;

// Acquire lock
void lock_acquire(void) {
    while (atomic_flag_test_and_set(&lock)) {
        cpu_relax();  // Reduce power consumption
    }
}

// Release lock
void lock_release(void) {
    atomic_flag_clear(&lock);
}
```

### Load/Store Operations

```c
// Atomically load value from obj
int atomic_load(const volatile atomic_int *obj);
int atomic_load_explicit(const volatile atomic_int *obj, memory_order order);

// Atomically store desired to obj
void atomic_store(volatile atomic_int *obj, int desired);
void atomic_store_explicit(volatile atomic_int *obj, int desired, memory_order order);
```

**Use Cases**
- `atomic_load`: Reading shared variables with synchronization
- `atomic_store`: Publishing results to other threads

### Exchange Operation

```c
// Atomically replace obj's value with desired, return previous value
int atomic_exchange(volatile atomic_int *obj, int desired);
int atomic_exchange_explicit(volatile atomic_int *obj, int desired, memory_order order);
```

**Use Case**: Swapping values, resetting to initial state

```c
// Reset counter to 0, return previous value
int old = atomic_exchange(&counter, 0);
```

### Compare-and-Swap (CAS) Operations

CAS is the fundamental primitive for lock-free algorithms. It atomically compares `*obj` to `*expected` and, if equal, stores `desired`.

```c
// Strong CAS: always succeeds if comparison matches
bool atomic_compare_exchange_strong(volatile atomic_int *obj, int *expected, int desired);
bool atomic_compare_exchange_strong_explicit(
    volatile atomic_int *obj, int *expected, int desired,
    memory_order success, memory_order failure);

// Weak CAS: may fail spuriously (use in loops)
bool atomic_compare_exchange_weak(volatile atomic_int *obj, int *expected, int desired);
bool atomic_compare_exchange_weak_explicit(
    volatile atomic_int *obj, int *expected, int desired,
    memory_order success, memory_order failure);
```

**Return Value**: `true` if swap occurred, `false` otherwise

**Example: Lock-Free Counter Increment**

```c
int lock_free_increment(atomic_int *counter) {
    int old, new;
    do {
        old = atomic_load(counter);
        new = old + 1;
    } while (!atomic_compare_exchange_weak(counter, &old, new));
    return new;
}
```

**Weak vs Strong**:
- Use `weak` in loops (may fail spuriously but is faster on some architectures)
- Use `strong` for non-looped operations

### Fetch-and-Modify Operations

These operations return the **previous** value before modification.

```c
// Return old value, then add arg
int atomic_fetch_add(volatile atomic_int *obj, int arg);
int atomic_fetch_add_explicit(volatile atomic_int *obj, int arg, memory_order order);

// Return old value, then subtract arg
int atomic_fetch_sub(volatile atomic_int *obj, int arg);
int atomic_fetch_sub_explicit(volatile atomic_int *obj, int arg, memory_order order);

// Return old value, then bitwise AND with arg
int atomic_fetch_and(volatile atomic_int *obj, int arg);
int atomic_fetch_and_explicit(volatile atomic_int *obj, int arg, memory_order order);

// Return old value, then bitwise OR with arg
int atomic_fetch_or(volatile atomic_int *obj, int arg);
int atomic_fetch_or_explicit(volatile atomic_int *obj, int arg, memory_order order);

// Return old value, then bitwise XOR with arg
int atomic_fetch_xor(volatile atomic_int *obj, int arg);
int atomic_fetch_xor_explicit(volatile atomic_int *obj, int arg, memory_order order);
```

**Example: Reference Counting**

```c
atomic_int refcount = ATOMIC_VAR_INIT(1);

void acquire(void) {
    atomic_fetch_add(&refcount, 1);
}

void release(void) {
    if (atomic_fetch_sub(&refcount, 1) == 1) {
        // Last reference freed
        free_resource();
    }
}
```

### Modify-and-Fetch Operations

These operations return the **new** value after modification.

```c
// Add arg, then return new value
int atomic_add_fetch(volatile atomic_int *obj, int arg);
int atomic_add_fetch_explicit(volatile atomic_int *obj, int arg, memory_order order);

// Subtract arg, then return new value
int atomic_sub_fetch(volatile atomic_int *obj, int arg);
int atomic_sub_fetch_explicit(volatile atomic_int *obj, int arg, memory_order order);
```

**Example: Sequential ID Allocation**

```c
atomic_int next_id = ATOMIC_VAR_INIT(0);

int allocate_id(void) {
    return atomic_add_fetch(&next_id, 1);
}
```

### Fences

Fences synchronize memory without operating on specific variables.

```c
// Thread fence: synchronizes with other threads
void atomic_thread_fence(memory_order order);

// Signal fence: synchronizes with signal handler
void atomic_signal_fence(memory_order order);
```

**Example**: Publishing data with release/acquire

```c
// Thread 1: Producer
data->value = 42;
atomic_thread_fence(memory_order_release);
data->ready = true;

// Thread 2: Consumer
if (atomic_load_explicit(&data->ready, memory_order_acquire)) {
    atomic_thread_fence(memory_order_acquire);
    use(data->value);  // Guaranteed to see 42
}
```

## x86_64 Optimized Operations

The architecture-specific header (`arch/x86_64/atomic.h`) provides optimized implementations:

```c
// Direct increment/decrement (no return value)
void atomic_inc(volatile atomic_int *v);
void atomic_dec(volatile atomic_int *v);

// Optimized add/subtract using LOCK XADD
int atomic_fetch_add(volatile atomic_int *obj, int arg);
int atomic_fetch_sub(volatile atomic_int *obj, int arg);
```

These use inline assembly with the `LOCK` prefix for atomicity on x86_64.

## Best Practices

1. **Use the weakest memory order that works**: Start with `seq_cst`, then relax to `acquire`/`release` if needed for performance.

2. **Prefer `fetch_*` over manual CAS loops**: Built-in operations may be more efficient.

3. **Use `atomic_flag` for simple spin locks**: It's guaranteed lock-free.

4. **Always pair acquire with release**: An acquire must synchronize-with a corresponding release.

5. **Use `cpu_relax()` in spin loops**: Reduces power consumption and improves hyperthreading performance.

6. **Beware of ABA problem**: CAS can't distinguish a value changing back to the original. Use version counters if needed.

## Performance Considerations

| Operation | Relative Cost |
|-----------|---------------|
| `relaxed` operations | 1x (baseline) |
| `acquire`/`release` | 1x-2x |
| `seq_cst` operations | 2x-3x |
| CAS loop (contended) | 10x-100x+ |

On x86_64, the `LOCK` prefix ensures atomicity and implicitly acts as a full memory barrier.

## See Also

- `docs/barrier.md` - Memory barrier API
- `arch/x86_64/spinlock_arch.h` - Spin lock implementations
- `kernel/smp.c` - SMP and per-CPU data usage
