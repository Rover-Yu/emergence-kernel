/* JAKernel - Architecture-Independent Spin Lock Interface */

#ifndef _KERNEL_SPINLOCK_H
#define _KERNEL_SPINLOCK_H

#include <stdint.h>

/* Opaque types - actual definitions in arch-specific headers */
typedef struct arch_spinlock spinlock_t;
typedef struct arch_rwlock rwlock_t;
typedef unsigned long irq_flags_t;

/* Include architecture-specific implementation */
#ifdef __x86_64__
#include "arch/x86_64/spinlock_arch.h"
#else
#error "Unsupported architecture"
#endif

/* ============================================================================
 * Public API - Basic Spin Locks
 * ============================================================================ */

/**
 * spin_lock_init - Initialize a spin lock
 * @lock: Lock to initialize
 */
static inline void spin_lock_init(spinlock_t *lock) {
    arch_spin_lock_init(lock);
}

/**
 * spin_lock - Acquire a spin lock
 * @lock: Lock to acquire
 *
 * Spins until the lock becomes available.
 */
static inline void spin_lock(spinlock_t *lock) {
    arch_spin_lock(lock);
}

/**
 * spin_unlock - Release a spin lock
 * @lock: Lock to release
 */
static inline void spin_unlock(spinlock_t *lock) {
    arch_spin_unlock(lock);
}

/**
 * spin_trylock - Try to acquire a spin lock without waiting
 * @lock: Lock to try to acquire
 *
 * Returns: 1 if lock was acquired, 0 if lock is already held
 */
static inline int spin_trylock(spinlock_t *lock) {
    return arch_spin_trylock(lock);
}

/* ============================================================================
 * Public API - Interrupt-Safe Spin Locks
 * ============================================================================ */

/**
 * spin_lock_irqsave - Acquire lock and save/disable interrupts
 * @lock: Lock to acquire
 * @flags: Pointer to store interrupt flags
 *
 * Saves the current interrupt state and disables interrupts before
 * acquiring the lock. Use this when the lock could be accessed from
 * interrupt context to prevent deadlocks.
 *
 * Example:
 *   irq_flags_t flags;
 *   spin_lock_irqsave(&my_lock, &flags);
 *   // ... critical section ...
 *   spin_unlock_irqrestore(&my_lock, &flags);
 */
static inline void spin_lock_irqsave(spinlock_t *lock, irq_flags_t *flags) {
    arch_spin_lock_irqsave(lock, flags);
}

/**
 * spin_unlock_irqrestore - Release lock and restore interrupt state
 * @lock: Lock to release
 * @flags: Previously saved interrupt flags
 *
 * Releases the lock and restores interrupts to their previous state.
 */
static inline void spin_unlock_irqrestore(spinlock_t *lock, irq_flags_t *flags) {
    arch_spin_unlock_irqrestore(lock, flags);
}

/**
 * spin_lock_irq - Acquire lock with interrupts disabled
 * @lock: Lock to acquire
 *
 * Disables interrupts and acquires the lock.
 * Does not save interrupt state - use when you know interrupts were enabled.
 */
static inline void spin_lock_irq(spinlock_t *lock) {
    arch_spin_lock_irq(lock);
}

/**
 * spin_unlock_irq - Release lock and enable interrupts
 * @lock: Lock to release
 *
 * Releases the lock and unconditionally enables interrupts.
 */
static inline void spin_unlock_irq(spinlock_t *lock) {
    arch_spin_unlock_irq(lock);
}

/* ============================================================================
 * Public API - Read-Write Locks
 * ============================================================================ */

/**
 * rwlock_init - Initialize a read-write lock
 * @lock: Lock to initialize
 */
static inline void rwlock_init(rwlock_t *lock) {
    arch_rwlock_init(lock);
}

/**
 * spin_read_lock - Acquire lock for reading
 * @lock: Lock to acquire
 *
 * Multiple readers can hold the lock simultaneously.
 * Writers are excluded until all readers release the lock.
 */
static inline void spin_read_lock(rwlock_t *lock) {
    arch_spin_read_lock(lock);
}

/**
 * spin_read_unlock - Release read lock
 * @lock: Lock to release
 */
static inline void spin_read_unlock(rwlock_t *lock) {
    arch_spin_read_unlock(lock);
}

/**
 * spin_write_lock - Acquire lock for writing
 * @lock: Lock to acquire
 *
 * Exclusive access - no readers or other writers allowed.
 */
static inline void spin_write_lock(rwlock_t *lock) {
    arch_spin_write_lock(lock);
}

/**
 * spin_write_unlock - Release write lock
 * @lock: Lock to release
 */
static inline void spin_write_unlock(rwlock_t *lock) {
    arch_spin_write_unlock(lock);
}

/* ============================================================================
 * Test Functions
 * ============================================================================ */

/**
 * run_spinlock_tests - Run all spin lock tests
 *
 * Runs comprehensive spin lock and read-write lock tests including:
 * - Single-CPU tests (basic operations, trylock, IRQ-safe, RWLock)
 * - SMP multi-CPU tests (contention, deadlock prevention)
 *
 * Returns: Number of failed tests (0 = all passed)
 */
extern int run_spinlock_tests(void);

/**
 * spinlock_test_ap_entry - AP entry point for SMP tests
 *
 * Called by APs when spinlock_test_start is set.
 * APs participate in SMP tests coordinated by BSP.
 */
extern void spinlock_test_ap_entry(void);

/**
 * spinlock_test_start - Flag to signal APs to join tests
 *
 * Set by BSP to 1 to activate test mode.
 * APs check this flag in ap_start() before halting.
 */
extern volatile int spinlock_test_start;

#endif /* _KERNEL_SPINLOCK_H */
