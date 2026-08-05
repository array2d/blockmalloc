#ifndef SPINLOCK_H
#define SPINLOCK_H

#include <stdint.h>

#endif // SPINLOCK_H

#ifdef BLOCKMALLOC_IMPLEMENTATION
#ifndef BLOCKMALLOC_SPINLOCK_IMPL_DONE
#define BLOCKMALLOC_SPINLOCK_IMPL_DONE

#if defined(_MSC_VER)
#include <windows.h>
#include <intrin.h>
#endif

/* architecture-specific CPU-relax / pause */
#if defined(__x86_64__) || defined(__i386__)
static inline void cpu_relax(void) { __asm__ __volatile__("pause" ::: "memory"); }
#elif defined(__aarch64__) || defined(__arm__)
static inline void cpu_relax(void) { __asm__ __volatile__("yield" ::: "memory"); }
#elif defined(_MSC_VER)
static inline void cpu_relax(void) { YieldProcessor(); }
#else
static inline void cpu_relax(void) { (void)0; }
#endif

/* atomic exchange / store helpers (platform specific) */
#ifdef _MSC_VER
static inline int64_t spin_atomic_exchange(int64_t *ptr, int64_t val)
{
    /* InterlockedExchange64 works with LONG64; cast to/from int64_t */
    return (int64_t)InterlockedExchange64((volatile LONG64 *)ptr, (LONG64)val);
}
static inline void spin_atomic_store(int64_t *ptr, int64_t val)
{
    /* Use InterlockedExchange64 as a store (returns previous, ignore it) */
    InterlockedExchange64((volatile LONG64 *)ptr, (LONG64)val);
}
#else
static inline int64_t spin_atomic_exchange(int64_t *ptr, int64_t val)
{
    return __atomic_exchange_n((int64_t *)ptr, val, __ATOMIC_ACQ_REL);
}
static inline void spin_atomic_store(int64_t *ptr, int64_t val)
{
    __atomic_store_n((int64_t *)ptr, val, __ATOMIC_RELEASE);
}
#endif

static inline void spin_lock(int64_t *lock)
{
    while (spin_atomic_exchange(lock, 1))
    {
        cpu_relax();
    }
}

static inline void spin_unlock(int64_t *lock)
{
    spin_atomic_store(lock, 0);
}

#endif
#endif