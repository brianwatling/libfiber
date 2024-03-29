// SPDX-FileCopyrightText: 2012-2023 Brian Watling <brian@oxbo.dev>
// SPDX-License-Identifier: MIT

#ifndef _FIBER_MACHINE_SPECIFIC_H_
#define _FIBER_MACHINE_SPECIFIC_H_

#ifndef FIBER_CACHELINE_SIZE
#if defined(__i386__) || defined(__x86_64__)
#define FIBER_CACHELINE_SIZE (64)
#else
#error please define a FIBER_CACHELINE_SIZE
#endif
#endif

#include <stdatomic.h>
#include <stdint.h>

_Static_assert(ATOMIC_BOOL_LOCK_FREE == 2, "");
_Static_assert(ATOMIC_CHAR_LOCK_FREE == 2, "");
_Static_assert(ATOMIC_CHAR16_T_LOCK_FREE == 2, "");
_Static_assert(ATOMIC_CHAR32_T_LOCK_FREE == 2, "");
_Static_assert(ATOMIC_WCHAR_T_LOCK_FREE == 2, "");
_Static_assert(ATOMIC_SHORT_LOCK_FREE == 2, "");
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "");
_Static_assert(ATOMIC_LONG_LOCK_FREE == 2, "");
_Static_assert(ATOMIC_LLONG_LOCK_FREE == 2, "");
_Static_assert(ATOMIC_POINTER_LOCK_FREE == 2, "");

/* this barrier orders writes against other writes */
static inline void write_barrier() {
#if defined(__i386__) || defined(__x86_64__)
  __asm__ __volatile__("" : : : "memory");
#else
#error please define a write_barrier()
#endif
}

/* this barrier orders writes against reads */
static inline void store_load_barrier() {
#if defined(__i386__)
  __asm__ __volatile__("lock; addl $0,0(%%esp)" : : : "memory");
#elif defined(__x86_64__)
  __asm__ __volatile__("lock; addq $0,0(%%rsp)" : : : "memory");
#else
#error please define a store_load_barrier()
#endif
}

/* this barrier orders loads against other loads */
static inline void load_load_barrier() {
#if defined(__i386__) || defined(__x86_64__)
  __asm__ __volatile__("" : : : "memory");
#else
#error please define a load_load_barrier
#endif
}

static inline void cpu_relax() {
#if defined(__i386__) || defined(__x86_64__)
  __asm__ __volatile__("pause" : : : "memory");
#else
#warning no cpu_relax() defined for this architecture. please consider defining one if possible.
#endif
}

typedef struct pointer_pair {
  void* low;
  void* high;
} __attribute__((__aligned__(2 * sizeof(void*)))) __attribute__((__packed__))
pointer_pair_t;

static inline int compare_and_swap2(volatile pointer_pair_t* location,
                                    const pointer_pair_t* original_value,
                                    const pointer_pair_t* new_value) {
#if defined(__i386__)
  return __sync_bool_compare_and_swap(
      (uint64_t*)location, *(uint64_t*)original_value, *(uint64_t*)new_value);
#elif defined(__x86_64__)
  char result;
  __asm__ __volatile__(
      "lock cmpxchg16b %1\n\t"
      "setz %0"
      : "=q"(result), "+m"(*location)
      : "d"(original_value->high), "a"(original_value->low),
        "c"(new_value->high), "b"(new_value->low)
      : "cc");
  return result;
#else
#error please define a compare_and_swap2()
#endif
}

#define fiber_unlikely(x) (__builtin_expect((x), 0))
#define fiber_likely(x) (__builtin_expect((x), 1))

#endif  // _FIBER_MACHINE_SPECIFIC_H_
