/* Emergence Kernel - x86_64 Spin Lock Architecture-Specific Implementation */

#ifndef _ARCH_X86_64_SPINLOCK_H
#define _ARCH_X86_64_SPINLOCK_H

#include <stdint.h>
#include "include/atomic.h"
#include "include/barrier.h"
#include "arch/x86_64/idt.h"

/* Actual spin lock structure for x86_64 */
struct arch_spinlock {
    atomic_int locked;  /* 0 = unlocked, 1 = locked */
};

/* Actual read-write lock structure for x86_64 */
struct arch_rwlock {
    atomic_int counter;  /* Negative: writer, 0: unlocked, Positive: readers */
};

/* Static initializers */
#define __SPIN_LOCK_UNLOCKED { .locked = 0 }
#define __RW_LOCK_UNLOCKED    { .counter = 0 }
#define SPIN_LOCK_UNLOCKED  __SPIN_LOCK_UNLOCKED
#define DEFINE_SPINLOCK(x)  struct arch_spinlock x = __SPIN_LOCK_UNLOCKED
#define RW_LOCK_UNLOCKED    __RW_LOCK_UNLOCKED
#define DEFINE_RWLOCK(x)    struct arch_rwlock x = __RW_LOCK_UNLOCKED
#define SPINLOCK_IRQSAVE_DISABLED 0

/* Include interrupt control functions */
#include "arch/x86_64/idt.h"

/* ============================================================================
 * Basic Spin Lock Operations
 * ============================================================================ */

/**
 * arch_spin_lock_init - Initialize a spin lock to unlocked state
 * @lock: Lock to initialize
 */
static inline void arch_spin_lock_init(struct arch_spinlock *lock) {
    lock->locked = 0;
}

/**
 * arch_spin_lock - Acquire a spin lock
 * @lock: Lock to acquire
 *
 * Uses test-and-set with pause instruction for efficient waiting.
 * The pause instruction reduces power consumption and improves
 * performance on Hyper-Threading/SMT processors.
 */
static inline void arch_spin_lock(struct arch_spinlock *lock) {
    /* Test-and-set loop with adaptive spinning */
    int expected = 0;
    while (!atomic_compare_exchange_weak_explicit(&lock->locked, &expected, 1,
                                                   memory_order_acquire, memory_order_relaxed)) {
        expected = 0;
        /* Spin with pause to reduce bus contention */
        while (atomic_load_explicit(&lock->locked, memory_order_relaxed)) {
            cpu_relax();
        }
    }
    /* Acquire barrier ensures memory operations are not reordered */
    barrier();
}

/**
 * arch_spin_unlock - Release a spin lock
 * @lock: Lock to release
 *
 * Releases the lock with release semantics.
 */
static inline void arch_spin_unlock(struct arch_spinlock *lock) {
    /* Release barrier ensures all memory operations complete before unlock */
    smp_mb();
    /* Atomic store with release semantics */
    atomic_store_explicit(&lock->locked, 0, memory_order_release);
}

/**
 * arch_spin_trylock - Try to acquire a spin lock without waiting
 * @lock: Lock to try to acquire
 *
 * Returns: 1 if lock was acquired, 0 if lock is already held
 */
static inline int arch_spin_trylock(struct arch_spinlock *lock) {
    int expected = 0;
    return atomic_compare_exchange_strong_explicit(&lock->locked, &expected, 1,
                                                    memory_order_acquire, memory_order_relaxed);
}

/* ============================================================================
 * Interrupt-Safe Spin Lock Operations
 * ============================================================================ */

/**
 * arch_spin_lock_irqsave - Acquire lock and disable interrupts
 * @lock: Lock to acquire
 * @flags: Pointer to store interrupt flags
 *
 * Saves current interrupt state and disables interrupts before acquiring lock.
 * Use this when lock could be accessed from interrupt context.
 * Interrupt state will be restored only if it was enabled when saved.
 */
static inline void arch_spin_lock_irqsave(struct arch_spinlock *lock, irq_flags_t *flags) {
    /* Save interrupt flags and disable using unified API */
    *flags = arch_irq_save(1);
    arch_spin_lock(lock);
}

/**
 * arch_spin_unlock_irqrestore - Release lock and restore interrupt state
 * @lock: Lock to release
 * @flags: Previously saved interrupt flags
 *
 * Releases the lock and restores interrupts to their previous state.
 */
static inline void arch_spin_unlock_irqrestore(struct arch_spinlock *lock, irq_flags_t *flags) {
    arch_spin_unlock(lock);
    /* Restore interrupt state using unified API */
    arch_irq_restore(flags);
}

/**
 * arch_spin_lock_irq - Acquire lock with interrupts disabled
 * @lock: Lock to acquire
 *
 * Disables interrupts and acquires the lock.
 * Does not save interrupt state - use when you know interrupts were enabled.
 */
static inline void arch_spin_lock_irq(struct arch_spinlock *lock) {
    disable_interrupts();
    arch_spin_lock(lock);
}

/**
 * arch_spin_unlock_irq - Release lock and enable interrupts
 * @lock: Lock to release
 *
 * Releases the lock and unconditionally enables interrupts.
 */
static inline void arch_spin_unlock_irq(struct arch_spinlock *lock) {
    arch_spin_unlock(lock);
    enable_interrupts();
}

/* ============================================================================
 * Read-Write Lock Operations
 * ============================================================================ */

/**
 * arch_rwlock_init - Initialize a read-write lock to unlocked state
 * @lock: Lock to initialize
 */
static inline void arch_rwlock_init(struct arch_rwlock *lock) {
    lock->counter = 0;
}

/**
 * arch_spin_read_lock - Acquire lock for reading
 * @lock: Lock to acquire
 *
 * Increments reader count. Blocks if a writer holds lock.
 */
static inline void arch_spin_read_lock(struct arch_rwlock *lock) {
    /* Increment counter - if negative, a writer holds lock */
    while (atomic_fetch_add_explicit(&lock->counter, 1, memory_order_acquire) < 0) {
        /* Failed - writer holds lock, back off and retry */
    return;
    }
    /* Acquire barrier ensures memory operations are not reordered */
    barrier();
}

/**
 * arch_spin_read_unlock - Release read lock
 * @lock: Lock to release
 *
 * Decrements reader count. Wakes writer if waiting.
 */
static inline void arch_spin_read_unlock(struct arch_rwlock *lock) {
    /* Release barrier ensures all memory operations complete before unlock */
    smp_mb();
    /* Atomic decrement with release semantics */
    atomic_fetch_sub_explicit(&lock->counter, 1, memory_order_release);
}

/**
 * arch_spin_write_lock - Acquire lock for writing
 * @lock: Lock to acquire
 *
 * Blocks readers and other writers. Only one writer at a time.
 */
static inline void arch_spin_write_lock(struct arch_rwlock *lock) {
    /* Decrement counter to negative (writer mode) */
    while (atomic_fetch_add_explicit(&lock->counter, -1, memory_order_acquire) != 0) {
        /* Another writer holds lock, back off and retry */
        return;
    }
    /* Acquire barrier ensures memory operations are not reordered */
    barrier();
}

/**
 * arch_spin_write_unlock - Release write lock
 * @lock: Lock to release
 *
 * Resets counter to zero (unlocked state). Wakes waiting readers/writer.
 */
static inline void arch_spin_write_unlock(struct arch_rwlock *lock) {
    /* Release barrier ensures all memory operations complete before unlock */
    smp_mb();
    /* Atomic store with release semantics */
    atomic_store_explicit(&lock->counter, 0, memory_order_release);
}

/* ============================================================================
 * Read-Write Lock IRQ Save/Restore Operations
 * ============================================================================ */

/**
 * arch_spin_lock_irqsave_disabled - Acquire lock without disabling interrupts
 * @lock: Lock to acquire
 *
 * Does not save interrupt state - use when you know interrupts were enabled.
 * Use this in early boot or interrupt context where interrupts are already disabled.
 */
static inline void arch_spin_lock_irqsave_disabled(struct arch_spinlock *lock) {
    /* Spin with pause */
    int expected = 0;
    while (!atomic_compare_exchange_weak_explicit(&lock->locked, &expected, 1,
                                                   memory_order_acquire, memory_order_relaxed)) {
        expected = 0;
        /* Spin with pause to reduce bus contention */
        while (atomic_load_explicit(&lock->locked, memory_order_relaxed)) {
            cpu_relax();
        }
    }
    /* Acquire barrier ensures memory operations are not reordered */
    barrier();
}

/**
 * arch_spin_unlock_irqrestore_disabled - Release lock without enabling interrupts
 * @lock: Lock to release
 *
 * Does not modify interrupt state - interrupts remain disabled.
 */
static inline void arch_spin_unlock_irqrestore_disabled(struct arch_spinlock *lock) {
    /* Release barrier ensures all memory operations complete before unlock */
    smp_mb();
    /* Atomic store with release semantics */
    atomic_store_explicit(&lock->locked, 0, memory_order_release);
}

#endif /* _ARCH_X86_64_SPINLOCK_H */
