// SPDX-FileCopyrightText: 2012-2023 Brian Watling <brian@oxbo.dev>
// SPDX-License-Identifier: MIT

#include "fiber_spinlock.h"

#include "fiber.h"
#include "fiber_manager.h"
#include "sched.h"

#ifdef __GNUC__
#define STATIC_ASSERT_HELPER(expr, msg) \
  (!!sizeof(struct { unsigned int STATIC_ASSERTION__##msg : (expr) ? 1 : -1; }))
#define STATIC_ASSERT(expr, msg) \
  extern int(*assert_function__(void))[STATIC_ASSERT_HELPER(expr, msg)]
#else
#define STATIC_ASSERT(expr, msg)          \
  extern char STATIC_ASSERTION__##msg[1]; \
  extern char STATIC_ASSERTION__##msg[(expr) ? 1 : 2]
#endif /* #ifdef __GNUC__ */

STATIC_ASSERT(sizeof(fiber_spinlock_internal_t) == sizeof(uint64_t),
              state_is_not_sized_properly);

int fiber_spinlock_init(fiber_spinlock_t* spinlock) {
  assert(spinlock);
  spinlock->state.blob = 0;
  return FIBER_SUCCESS;
}

int fiber_spinlock_destroy(fiber_spinlock_t* spinlock) {
  assert(spinlock);
  return FIBER_SUCCESS;
}

int fiber_spinlock_lock(fiber_spinlock_t* spinlock) {
  assert(spinlock);

  const uint32_t my_ticket = atomic_fetch_add_explicit(
      &spinlock->state.counters.users, 1, memory_order_acquire);
  while (atomic_load_explicit(&spinlock->state.counters.ticket,
                              memory_order_acquire) != my_ticket) {
    cpu_relax();
    fiber_manager_get()->spin_count += 1;
  }

  return FIBER_SUCCESS;
}

int fiber_spinlock_trylock(fiber_spinlock_t* spinlock) {
  assert(spinlock);

  fiber_spinlock_internal_t old;
  old.blob = spinlock->state.blob;
  old.counters.ticket = old.counters.users;
  fiber_spinlock_internal_t new;
  new.blob = old.blob;
  new.counters.users += 1;
  if (!atomic_compare_exchange_weak_explicit(
          &spinlock->state.blob, (uint64_t*)&old.blob, new.blob,
          memory_order_acquire, memory_order_relaxed)) {
    return FIBER_ERROR;
  }

  return FIBER_SUCCESS;
}

int fiber_spinlock_unlock(fiber_spinlock_t* spinlock) {
  assert(spinlock);

  const uint32_t old_ticket = atomic_load_explicit(
      &spinlock->state.counters.ticket, memory_order_acquire);
  atomic_store_explicit(&spinlock->state.counters.ticket, old_ticket + 1,
                        memory_order_release);

  return FIBER_SUCCESS;
}
