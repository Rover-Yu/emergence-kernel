/* Emergence Kernel - Atomic Operations API */

#ifndef _EMERGENCE_ATOMIC_H
#define _EMERGENCE_ATOMIC_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * Memory Order Enum (C11-compatible)
 * ============================================================================ */

typedef enum {
    memory_order_relaxed = __ATOMIC_RELAXED,
    memory_order_consume = __ATOMIC_CONSUME,
    memory_order_acquire = __ATOMIC_ACQUIRE,
    memory_order_release = __ATOMIC_RELEASE,
    memory_order_acq_rel = __ATOMIC_ACQ_REL,
    memory_order_seq_cst = __ATOMIC_SEQ_CST
} memory_order;

/* ============================================================================
 * Atomic Types
 * ============================================================================ */

typedef volatile _Atomic int              atomic_int;
typedef volatile _Atomic unsigned int     atomic_uint;
typedef volatile _Atomic long             atomic_long;
typedef volatile _Atomic unsigned long    atomic_ulong;
typedef volatile _Atomic intptr_t         atomic_intptr_t;
typedef volatile _Atomic uintptr_t        atomic_uintptr_t;
typedef volatile _Atomic _Bool            atomic_bool;

/* ATOMIC_VAR_INIT - Initialize atomic variables (for static initialization) */
#define ATOMIC_VAR_INIT(value) (value)

/* ============================================================================
 * Atomic Flag (Lock-free spin primitive)
 * ============================================================================ */

typedef volatile _Atomic _Bool atomic_flag;

#define ATOMIC_FLAG_INIT false

static inline bool atomic_flag_test_and_set(volatile atomic_flag *obj) {
    return __atomic_test_and_set(obj, memory_order_seq_cst);
}

static inline bool atomic_flag_test_and_set_explicit(volatile atomic_flag *obj, memory_order order) {
    return __atomic_test_and_set(obj, order);
}

static inline void atomic_flag_clear(volatile atomic_flag *obj) {
    __atomic_clear(obj, memory_order_seq_cst);
}

static inline void atomic_flag_clear_explicit(volatile atomic_flag *obj, memory_order order) {
    __atomic_clear(obj, order);
}

/* ============================================================================
 * Load/Store Operations
 * ============================================================================ */

static inline int atomic_load(const volatile atomic_int *obj) {
    return __atomic_load_n(obj, memory_order_seq_cst);
}

static inline int atomic_load_explicit(const volatile atomic_int *obj, memory_order order) {
    return __atomic_load_n(obj, order);
}

static inline void atomic_store(volatile atomic_int *obj, int desired) {
    __atomic_store_n(obj, desired, memory_order_seq_cst);
}

static inline void atomic_store_explicit(volatile atomic_int *obj, int desired, memory_order order) {
    __atomic_store_n(obj, desired, order);
}

/* ============================================================================
 * Exchange Operations
 * ============================================================================ */

static inline int atomic_exchange(volatile atomic_int *obj, int desired) {
    return __atomic_exchange_n(obj, desired, memory_order_seq_cst);
}

static inline int atomic_exchange_explicit(volatile atomic_int *obj, int desired, memory_order order) {
    return __atomic_exchange_n(obj, desired, order);
}

/* ============================================================================
 * Compare-and-Swap Operations
 * ============================================================================ */

static inline bool atomic_compare_exchange_strong(volatile atomic_int *obj, int *expected, int desired) {
    return __atomic_compare_exchange_n(obj, expected, desired, false,
                                        memory_order_seq_cst, memory_order_seq_cst);
}

static inline bool atomic_compare_exchange_strong_explicit(volatile atomic_int *obj, int *expected, int desired,
                                                           memory_order success, memory_order failure) {
    return __atomic_compare_exchange_n(obj, expected, desired, false, success, failure);
}

static inline bool atomic_compare_exchange_weak(volatile atomic_int *obj, int *expected, int desired) {
    return __atomic_compare_exchange_n(obj, expected, desired, true,
                                        memory_order_seq_cst, memory_order_seq_cst);
}

static inline bool atomic_compare_exchange_weak_explicit(volatile atomic_int *obj, int *expected, int desired,
                                                         memory_order success, memory_order failure) {
    return __atomic_compare_exchange_n(obj, expected, desired, true, success, failure);
}

/* ============================================================================
 * Fetch-and-Modify Operations
 * ============================================================================ */

static inline int atomic_fetch_add(volatile atomic_int *obj, int arg) {
    return __atomic_fetch_add(obj, arg, memory_order_seq_cst);
}

static inline int atomic_fetch_add_explicit(volatile atomic_int *obj, int arg, memory_order order) {
    return __atomic_fetch_add(obj, arg, order);
}

static inline int atomic_fetch_sub(volatile atomic_int *obj, int arg) {
    return __atomic_fetch_sub(obj, arg, memory_order_seq_cst);
}

static inline int atomic_fetch_sub_explicit(volatile atomic_int *obj, int arg, memory_order order) {
    return __atomic_fetch_sub(obj, arg, order);
}

static inline int atomic_fetch_and(volatile atomic_int *obj, int arg) {
    return __atomic_fetch_and(obj, arg, memory_order_seq_cst);
}

static inline int atomic_fetch_and_explicit(volatile atomic_int *obj, int arg, memory_order order) {
    return __atomic_fetch_and(obj, arg, order);
}

static inline int atomic_fetch_or(volatile atomic_int *obj, int arg) {
    return __atomic_fetch_or(obj, arg, memory_order_seq_cst);
}

static inline int atomic_fetch_or_explicit(volatile atomic_int *obj, int arg, memory_order order) {
    return __atomic_fetch_or(obj, arg, order);
}

static inline int atomic_fetch_xor(volatile atomic_int *obj, int arg) {
    return __atomic_fetch_xor(obj, arg, memory_order_seq_cst);
}

static inline int atomic_fetch_xor_explicit(volatile atomic_int *obj, int arg, memory_order order) {
    return __atomic_fetch_xor(obj, arg, order);
}

/* ============================================================================
 * Modify-and-Fetch Operations
 * ============================================================================ */

static inline int atomic_add_fetch(volatile atomic_int *obj, int arg) {
    return __atomic_add_fetch(obj, arg, memory_order_seq_cst);
}

static inline int atomic_add_fetch_explicit(volatile atomic_int *obj, int arg, memory_order order) {
    return __atomic_add_fetch(obj, arg, order);
}

static inline int atomic_sub_fetch(volatile atomic_int *obj, int arg) {
    return __atomic_sub_fetch(obj, arg, memory_order_seq_cst);
}

static inline int atomic_sub_fetch_explicit(volatile atomic_int *obj, int arg, memory_order order) {
    return __atomic_sub_fetch(obj, arg, order);
}

/* ============================================================================
 * Thread and Signal Fences
 * ============================================================================ */

static inline void atomic_thread_fence(memory_order order) {
    __atomic_thread_fence(order);
}

static inline void atomic_signal_fence(memory_order order) {
    __atomic_signal_fence(order);
}

#endif /* _EMERGENCE_ATOMIC_H */
