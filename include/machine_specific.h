#ifndef _FIBER_MACHINE_SPECIFIC_H_
#define _FIBER_MACHINE_SPECIFIC_H_

#ifndef CACHE_SIZE
#if defined(ARCH_x86) || defined(ARCH_x86_64)
    #define CACHE_SIZE (64)
#else
    #error please define a CACHE_SIZE
#endif
#endif

/* this barrier orders writes against other writes */
static inline void write_barrier()
{
#if defined(ARCH_x86) || defined(ARCH_x86_64)
    __asm__ __volatile__ ("" : : : "memory");
#else
    #error please define a write_barrier()
#endif
}

/* this barrier orders writes against reads */
static inline void store_load_barrier()
{
#if defined(ARCH_x86)
    __asm__ __volatile__ ("lock; addl $0,0(%%esp)" : : : "memory");
#elif defined(ARCH_x86_64)
    __asm__ __volatile__ ("lock; addq $0,0(%%rsp)" : : : "memory");
#else
    #error please define a store_load_barrier()
#endif
}

/* this barrier orders loads against other loads */
static inline void load_load_barrier()
{
#if defined(ARCH_x86) || defined(ARCH_x86_64)
    __asm__ __volatile__ ("" : : : "memory");
#else
    #error please define a load_load_barrier
#endif
}

#if defined(ARCH_x86) || defined(ARCH_x86_64)
#define FIBER_XCHG_POINTER
static inline void* atomic_exchange_pointer(void** location, void* value)
{
    void* result = value;
    __asm__ volatile (
        "lock xchg %1,%0"
        :"+r" (result), "+m" (*location)
        : /* no input-only operands */
	);
    return result;
}

static inline int atomic_exchange_int(int* location, int value)
{
    int result = value;
    __asm__ volatile (
        "lock xchg %1,%0"
        :"+r" (result), "+m" (*location)
        : /* no input-only operands */
	);
    return result;
}
#else
#warning no atomic_exchange_pointer() defined for this architecture. please consider defining one if possible.
#define FIBER_NO_XCHG_POINTER
#endif

#endif

