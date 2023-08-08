// SPDX-FileCopyrightText: 2012-2023 Brian Watling <brian@oxbo.dev>
// SPDX-License-Identifier: MIT

#include "fiber_semaphore.h"

#include "fiber_manager.h"

int fiber_semaphore_init(fiber_semaphore_t* semaphore, int value) {
  assert(semaphore);
  semaphore->counter = value;
  mpmc_fifo_node_t* const initial_node = fiber_manager_get_mpmc_node();
  if (!mpmc_fifo_init(&semaphore->waiters, initial_node)) {
    fiber_manager_return_mpmc_node(initial_node);
    return FIBER_ERROR;
  }
  return FIBER_SUCCESS;
}

int fiber_semaphore_destroy(fiber_semaphore_t* semaphore) {
  assert(semaphore);
  semaphore->counter = 0;
  mpmc_fifo_destroy(fiber_manager_get_hazard_record(fiber_manager_get()),
                    &semaphore->waiters);
  return FIBER_SUCCESS;
}

int fiber_semaphore_wait(fiber_semaphore_t* semaphore) {
  assert(semaphore);

  const int val = atomic_fetch_sub(&semaphore->counter, 1) - 1;
  if (val >= 0) {
    // we just got in, there was no contention
    return FIBER_SUCCESS;
  }

  // we didn't get in, we'll wait
  fiber_manager_wait_in_mpmc_queue(fiber_manager_get(), &semaphore->waiters);

  return FIBER_SUCCESS;
}

int fiber_semaphore_trywait(fiber_semaphore_t* semaphore) {
  assert(semaphore);

  int counter;
  while ((counter = atomic_load_explicit(&semaphore->counter,
                                         memory_order_acquire)) > 0) {
    if (atomic_compare_exchange_weak_explicit(&semaphore->counter, &counter,
                                              counter - 1, memory_order_release,
                                              memory_order_relaxed)) {
      return FIBER_SUCCESS;
    }
  }
  return FIBER_ERROR;
}

// returns 1 if another fiber was woken after releasing the semaphore, 0
// otherwise
int fiber_semaphore_post_internal(fiber_semaphore_t* semaphore) {
  assert(semaphore);

  // assumption: the atomic operations below provide read/write ordering (ie.
  // read and writes performed before posting actually occur before posting)

  int prev_counter;
  do {
    while ((prev_counter = atomic_load_explicit(&semaphore->counter,
                                                memory_order_acquire)) < 0) {
      // another fiber is waiting; attempt to schedule it to take this fiber's
      // place
      if (fiber_manager_wake_from_mpmc_queue(fiber_manager_get(),
                                             &semaphore->waiters, 0)) {
        atomic_fetch_add(&semaphore->counter, 1);
        return 1;
      }
    }
  } while (!atomic_compare_exchange_weak_explicit(
      &semaphore->counter, &prev_counter, prev_counter + 1,
      memory_order_release, memory_order_relaxed));

  return 0;
}

int fiber_semaphore_post(fiber_semaphore_t* semaphore) {
  const int had_waiters = fiber_semaphore_post_internal(semaphore);
  if (had_waiters) {
    // the semaphore was contended - be nice and let the waiter run
    fiber_yield();
  }
  return FIBER_SUCCESS;
}

int fiber_semaphore_getvalue(fiber_semaphore_t* semaphore) {
  assert(semaphore);
  return semaphore->counter;
}
