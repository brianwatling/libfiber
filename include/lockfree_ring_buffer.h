// SPDX-FileCopyrightText: 2012-2023 Brian Watling <brian@oxbo.dev>
// SPDX-License-Identifier: MIT

#ifndef _LOCK_FREE_RING_BUFFER_H_
#define _LOCK_FREE_RING_BUFFER_H_

#include <assert.h>
#include <malloc.h>
#include <stddef.h>
#include <stdint.h>

#include "machine_specific.h"

typedef struct lockfree_ring_buffer {
  // high and low are generally used together; no point putting them on separate
  // cache lines
  _Atomic uint64_t high;
  char _cache_padding1[FIBER_CACHELINE_SIZE - sizeof(uint64_t)];
  _Atomic uint64_t low;
  char _cache_padding2[FIBER_CACHELINE_SIZE - sizeof(uint64_t)];
  uint32_t size;
  uint32_t power_of_2_mod;
  // buffer must be last - it spills outside of this struct
  void* buffer[];
} lockfree_ring_buffer_t;

static inline lockfree_ring_buffer_t* lockfree_ring_buffer_create(
    uint32_t power_of_2_size) {
  assert(power_of_2_size && power_of_2_size < 32);
  const uint32_t size = 1 << power_of_2_size;
  const uint32_t required_size =
      sizeof(lockfree_ring_buffer_t) + size * sizeof(void*);
  lockfree_ring_buffer_t* const ret =
      (lockfree_ring_buffer_t*)calloc(1, required_size);
  if (ret) {
    ret->size = size;
    ret->power_of_2_mod = size - 1;
  }
  return ret;
}

static inline void lockfree_ring_buffer_destroy(lockfree_ring_buffer_t* rb) {
  free(rb);
}

static inline size_t lockfree_ring_buffer_size(
    const lockfree_ring_buffer_t* rb) {
  assert(rb);
  // read high first; make it look less than or equal to
  // its actual size
  const uint64_t high = atomic_load_explicit(&rb->high, memory_order_acquire);

  const int64_t size =
      high - atomic_load_explicit(&rb->low, memory_order_acquire);
  return size >= 0 ? size : 0;
}

static inline int lockfree_ring_buffer_trypush(lockfree_ring_buffer_t* rb,
                                               void* in) {
  assert(rb);
  assert(in);  // can't store NULLs; we rely on a NULL to indicate a spot in the
               // buffer has not been written yet

  // read low first; this means the buffer will appear
  // larger or equal to its actual size
  const uint64_t low = atomic_load_explicit(&rb->low, memory_order_acquire);

  uint64_t high = atomic_load_explicit(&rb->high, memory_order_acquire);
  const uint64_t index = high & rb->power_of_2_mod;
  if (!rb->buffer[index] && high - low < rb->size &&
      atomic_compare_exchange_weak_explicit(&rb->high, &high, high + 1,
                                            memory_order_release,
                                            memory_order_relaxed)) {
    rb->buffer[index] = in;
    return 1;
  }
  return 0;
}

static inline void lockfree_ring_buffer_push(lockfree_ring_buffer_t* rb,
                                             void* in) {
  while (!lockfree_ring_buffer_trypush(rb, in)) {
    if (rb->high - rb->low >= rb->size) {
      cpu_relax();  // the buffer is full
    }
  };
}

static inline void* lockfree_ring_buffer_trypop(lockfree_ring_buffer_t* rb) {
  assert(rb);
  // read high first; this means the buffer will appear
  // smaller or equal to its actual size
  const uint64_t high = atomic_load_explicit(&rb->high, memory_order_acquire);

  uint64_t low = atomic_load_explicit(&rb->low, memory_order_acquire);
  const uint64_t index = low & rb->power_of_2_mod;
  void* const ret = rb->buffer[index];
  if (ret && high > low &&
      atomic_compare_exchange_weak_explicit(&rb->low, &low, low + 1,
                                            memory_order_acquire,
                                            memory_order_relaxed)) {
    rb->buffer[index] = 0;
    return ret;
  }
  return NULL;
}

static inline void* lockfree_ring_buffer_pop(lockfree_ring_buffer_t* rb) {
  void* ret;
  while (!(ret = lockfree_ring_buffer_trypop(rb))) {
    if (rb->high <= rb->low) {
      cpu_relax();  // the buffer is empty
    }
  }
  return ret;
}

#endif
