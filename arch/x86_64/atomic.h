/* Emergence Kernel - x86_64 Optimized Atomic Operations */

#ifndef _ARCH_X86_64_ATOMIC_H
#define _ARCH_X86_64_ATOMIC_H

#include <stdint.h>

/* Include generic types for consistency */
typedef volatile _Atomic int              atomic_int;
typedef volatile _Atomic unsigned int     atomic_uint;
typedef volatile _Atomic long             atomic_long;
typedef volatile _Atomic unsigned long    atomic_ulong;
typedef volatile _Atomic intptr_t         atomic_intptr_t;
typedef volatile _Atomic uintptr_t        atomic_uintptr_t;
typedef volatile _Atomic _Bool            atomic_bool;

/* ============================================================================
 * x86_64 Optimized Operations - Same Interface as Generic API
 * ============================================================================ */

/* These provide optimized inline assembly versions with identical signatures.
 * Code can include either generic atomic.h or this arch-specific header. */

static inline int atomic_fetch_add(volatile atomic_int *obj, int arg) {
    int result;
    __asm__ __volatile__("lock; xaddl %1, %0"
                         : "=r" (result), "+m" (*obj)
                         : "0" (arg)
                         : "memory");
    return result;
}

static inline int atomic_fetch_sub(volatile atomic_int *obj, int arg) {
    return atomic_fetch_add(obj, -arg);
}

static inline int atomic_add_fetch(volatile atomic_int *obj, int arg) {
    return __sync_add_and_fetch(obj, arg);
}

static inline int atomic_sub_fetch(volatile atomic_int *obj, int arg) {
    return __sync_sub_and_fetch(obj, arg);
}

static inline void atomic_inc(volatile atomic_int *v) {
    __asm__ __volatile__("lock; incl %0" : "+m" (*v) :: "memory");
}

static inline void atomic_dec(volatile atomic_int *v) {
    __asm__ __volatile__("lock; decl %0" : "+m" (*v) :: "memory");
}

/* Note: Other operations fall back to GCC built-ins via generic atomic.h.
 * The key is maintaining IDENTICAL signatures for drop-in replacement. */

#endif /* _ARCH_X86_64_ATOMIC_H */
