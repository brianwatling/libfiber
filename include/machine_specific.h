/*
 * Copyright (c) 2012-2015, Brian Watling and other contributors
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _FIBER_MACHINE_SPECIFIC_H_
#define _FIBER_MACHINE_SPECIFIC_H_

#ifndef CACHE_SIZE
#if defined(ARCH_x86) || defined(ARCH_x86_64)
    #define CACHE_SIZE (64)
#else
    #error please define a CACHE_SIZE
#endif
#endif

#include <stdint.h>

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
    __asm__ __volatile__ (
        "lock xchg %1,%0"
        :"+r" (result), "+m" (*location)
        : /* no input-only operands */
	);
    return result;
}

static inline int atomic_exchange_int(int* location, int value)
{
    int result = value;
    __asm__ __volatile__ (
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

static inline void cpu_relax()
{
#if defined(ARCH_x86) || defined(ARCH_x86_64)
    __asm__ __volatile__ (
        "pause": : : "memory"
    );
#else
#warning no cpu_relax() defined for this architecture. please consider defining one if possible.
#endif
}

typedef struct pointer_pair
{
    void* low;
    void* high;
} __attribute__ ((__aligned__(2 * sizeof(void*)))) __attribute__ ((__packed__)) pointer_pair_t;

static inline int compare_and_swap2(volatile pointer_pair_t* location, const pointer_pair_t* original_value, const pointer_pair_t* new_value)
{
#if defined(ARCH_x86)
    return __sync_bool_compare_and_swap((uint64_t*)location, *(uint64_t*)original_value, *(uint64_t*)new_value);
#elif defined(ARCH_x86_64)
    char result;
    __asm__ __volatile__ (
        "lock cmpxchg16b %1\n\t"
        "setz %0"
        :  "=q" (result)
         , "+m" (*location)
        :  "d" (original_value->high)
         , "a" (original_value->low)
         , "c" (new_value->high)
         , "b" (new_value->low)
        : "cc"
    );
    return result;
#else
    #error please define a compare_and_swap2()
#endif
}

#define fiber_unlikely(x) (__builtin_expect((x),0))
#define fiber_likely(x) (__builtin_expect((x),1))

#endif // _FIBER_MACHINE_SPECIFIC_H_

