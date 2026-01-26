/* JAKernel - x86_64 Spin Lock Architecture-Specific Implementation */

#ifndef _ARCH_X86_64_SPINLOCK_H
#define _ARCH_X86_64_SPINLOCK_H

#include <stdint.h>

/* Actual spin lock structure for x86_64 */
struct arch_spinlock {
    volatile int locked;  /* 0 = unlocked, 1 = locked */
};

/* Actual read-write lock structure for x86_64 */
struct arch_rwlock {
    volatile int counter;  /* Negative: writer, 0: unlocked, Positive: readers */
};

/* Static initializers */
#define __SPIN_LOCK_UNLOCKED  { .locked = 0 }
#define __RW_LOCK_UNLOCKED    { .counter = 0 }

#define SPIN_LOCK_UNLOCKED  __SPIN_LOCK_UNLOCKED
#define DEFINE_SPINLOCK(x)  struct arch_spinlock x = __SPIN_LOCK_UNLOCKED
#define RW_LOCK_UNLOCKED    __RW_LOCK_UNLOCKED
#define DEFINE_RWLOCK(x)    struct arch_rwlock x = __RW_LOCK_UNLOCKED

/* Flags type for interrupt state saving */
typedef unsigned long irq_flags_t;

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
    while (!__sync_bool_compare_and_swap(&lock->locked, 0, 1)) {
        /* Spin with pause to reduce bus contention */
        while (__sync_fetch_and_add(&lock->locked, 0)) {
            asm volatile("pause" ::: "memory");
        }
    }
    /* Compiler barrier - ensures memory operations are not reordered */
    __asm__ __volatile__("" ::: "memory");
}

/**
 * arch_spin_unlock - Release a spin lock
 * @lock: Lock to release
 */
static inline void arch_spin_unlock(struct arch_spinlock *lock) {
    /* Compiler barrier - ensures all memory operations complete before unlock */
    __asm__ __volatile__("" ::: "memory");
    lock->locked = 0;
}

/**
 * arch_spin_trylock - Try to acquire a spin lock without waiting
 * @lock: Lock to try to acquire
 *
 * Returns: 1 if lock was acquired, 0 if lock is already held
 */
static inline int arch_spin_trylock(struct arch_spinlock *lock) {
    return __sync_bool_compare_and_swap(&lock->locked, 0, 1);
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
 * Use this when the lock could be accessed from interrupt context.
 */
static inline void arch_spin_lock_irqsave(struct arch_spinlock *lock, irq_flags_t *flags) {
    /* Save interrupt flags (pushf/popf) */
    asm volatile("pushf\npop %0" : "=rm"(*flags) :: "memory");
    disable_interrupts();
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
    /* Check IF flag (bit 9) - restore interrupts only if they were enabled */
    if (*flags & (1 << 9)) {
        enable_interrupts();
    }
}

/**
 * arch_spin_lock_irq - Acquire lock with interrupts disabled
 * @lock: Lock to acquire
 *
 * Disables interrupts and acquires lock.
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
 * Multiple readers can hold the lock simultaneously.
 * Writers are excluded.
 */
static inline void arch_spin_read_lock(struct arch_rwlock *lock) {
    /* Increment counter - if negative, a writer has the lock */
    while (__sync_fetch_and_add(&lock->counter, 1) < 0) {
        /* Failed - writer holds lock, back off and retry */
        __sync_fetch_and_sub(&lock->counter, 1);
        while (lock->counter < 0) {
            asm volatile("pause" ::: "memory");
        }
    }
    __asm__ __volatile__("" ::: "memory");
}

/**
 * arch_spin_read_unlock - Release read lock
 * @lock: Lock to release
 */
static inline void arch_spin_read_unlock(struct arch_rwlock *lock) {
    __asm__ __volatile__("" ::: "memory");
    __sync_fetch_and_sub(&lock->counter, 1);
}

/**
 * arch_spin_write_lock - Acquire lock for writing
 * @lock: Lock to acquire
 *
 * Exclusive access - no readers or other writers allowed.
 */
static inline void arch_spin_write_lock(struct arch_rwlock *lock) {
    /* Decrement counter - must reach -1 (no readers or other writers) */
    while (__sync_fetch_and_add(&lock->counter, -1) != 0) {
        /* Failed - others hold lock, back off and retry */
        __sync_fetch_and_add(&lock->counter, 1);
        while (lock->counter != 0) {
            asm volatile("pause" ::: "memory");
        }
    }
    __asm__ __volatile__("" ::: "memory");
}

/**
 * arch_spin_write_unlock - Release write lock
 * @lock: Lock to release
 */
static inline void arch_spin_write_unlock(struct arch_rwlock *lock) {
    __asm__ __volatile__("" ::: "memory");
    __sync_fetch_and_add(&lock->counter, 1);
}

#endif /* _ARCH_X86_64_SPINLOCK_H */
