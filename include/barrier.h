/* JAKernel - Memory Barrier API */

#ifndef _JAKERNEL_BARRIER_H
#define _JAKERNEL_BARRIER_H

/* Include atomic.h for memory_order types */
#include "atomic.h"

/* ============================================================================
 * Compiler Barrier
 * ============================================================================ */

#define barrier() __asm__ __volatile__("" ::: "memory")

/* ============================================================================
 * SMP Memory Barriers
 * ============================================================================ */

#define smp_mb()   __asm__ __volatile__("mfence" ::: "memory")
#define smp_rmb()  barrier()  /* x86 has strong read ordering */
#define smp_wmb()  barrier()  /* x86 has strong write ordering */
#define smp_read_barrier_depends() barrier()

/* ============================================================================
 * Acquire/Release Barriers
 * ============================================================================ */

#define smp_load_acquire(ptr) \
    ({ \
        typeof(*ptr) ___val = __atomic_load_n(ptr, memory_order_acquire); \
        barrier(); \
        ___val; \
    })

#define smp_store_release(ptr, val) \
    do { \
        barrier(); \
        __atomic_store_n(ptr, val, memory_order_release); \
    } while (0)

/* ============================================================================
 * CPU Relax for Spin-Wait Loops
 * ============================================================================ */

#define cpu_relax() __asm__ __volatile__("pause")
#define cpu_pause() cpu_relax()

/* ============================================================================
 * Volatile Access Helpers
 * ============================================================================ */

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

#endif /* _JAKERNEL_BARRIER_H */
