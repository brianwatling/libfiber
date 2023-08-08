// SPDX-FileCopyrightText: 2012-2023 Brian Watling <brian@oxbo.dev>
// SPDX-License-Identifier: MIT

#ifndef _FIBER_SPINLOCK_H_
#define _FIBER_SPINLOCK_H_

/*
    Description: A spin lock for fibers, based on the ticket lock algorithm
                 found at http://locklessinc.com/articles/locks/. This is meant
                 to be used in places where a fiber does not want to or cannot
                 perform a context switch.
*/

#include <stddef.h>
#include <stdint.h>

typedef union {
  struct {
    _Atomic uint32_t ticket;
    _Atomic uint32_t users;
  } counters;
  _Atomic uint64_t blob;
} fiber_spinlock_internal_t;

_Static_assert(sizeof(fiber_spinlock_internal_t) == sizeof(uint64_t),
               "expected fiber_spinlock_internal_t to be 8 bytes");

typedef struct fiber_spinlock {
  fiber_spinlock_internal_t state;
} fiber_spinlock_t;

_Static_assert(sizeof(fiber_spinlock_t) == sizeof(uint64_t),
               "expected fiber_spinlock_t to be 8 bytes");

#ifdef __cplusplus
extern "C" {
#endif

#define FIBER_SPINLOCK_INITIALIER \
  {}

extern int fiber_spinlock_init(fiber_spinlock_t* spinlock);

extern int fiber_spinlock_destroy(fiber_spinlock_t* spinlock);

extern int fiber_spinlock_lock(fiber_spinlock_t* spinlock);

extern int fiber_spinlock_trylock(fiber_spinlock_t* spinlock);

extern int fiber_spinlock_unlock(fiber_spinlock_t* spinlock);

#ifdef __cplusplus
}
#endif

#endif
