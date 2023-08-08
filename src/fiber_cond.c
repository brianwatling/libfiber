// SPDX-FileCopyrightText: 2012-2023 Brian Watling <brian@oxbo.dev>
// SPDX-License-Identifier: MIT

#include "fiber_cond.h"

#include "fiber_manager.h"

int fiber_cond_init(fiber_cond_t* cond) {
  assert(cond);
  memset(cond, 0, sizeof(*cond));
  if (!mpsc_fifo_init(&cond->waiters)) {
    return FIBER_ERROR;
  }
  if (!fiber_mutex_init(&cond->internal_mutex)) {
    mpsc_fifo_destroy(&cond->waiters);
    return FIBER_ERROR;
  }
  return FIBER_SUCCESS;
}

void fiber_cond_destroy(fiber_cond_t* cond) {
  assert(cond);
  fiber_mutex_destroy(&cond->internal_mutex);
  mpsc_fifo_destroy(&cond->waiters);
  memset(cond, 0, sizeof(*cond));
}

int fiber_cond_signal(fiber_cond_t* cond) {
  assert(cond);

  fiber_mutex_lock(&cond->internal_mutex);
  intptr_t new_val = atomic_fetch_sub(&cond->waiter_count, 1) - 1;
  if (new_val >= 0) {
    fiber_manager_wake_from_mpsc_queue(fiber_manager_get(), &cond->waiters, 1);
  } else {
    new_val = atomic_fetch_add(&cond->waiter_count, 1) + 1;
    assert(new_val >= 0);
  }
  fiber_mutex_unlock(&cond->internal_mutex);

  return FIBER_SUCCESS;
}

int fiber_cond_broadcast(fiber_cond_t* cond) {
  assert(cond);

  fiber_mutex_lock(&cond->internal_mutex);
  const intptr_t original =
      atomic_exchange_explicit(&cond->waiter_count, 0, memory_order_acquire);
  if (original) {
    fiber_manager_wake_from_mpsc_queue(fiber_manager_get(), &cond->waiters,
                                       original);
  }
  fiber_mutex_unlock(&cond->internal_mutex);

  return FIBER_SUCCESS;
}

int fiber_cond_wait(fiber_cond_t* cond, fiber_mutex_t* mutex) {
  assert(cond);
  assert(mutex);

  assert(!cond->caller_mutex || cond->caller_mutex == mutex);
  cond->caller_mutex = mutex;
  atomic_fetch_add_explicit(&cond->waiter_count, 1, memory_order_release);

  fiber_manager_wait_in_mpsc_queue_and_unlock(fiber_manager_get(),
                                              &cond->waiters, mutex);
  fiber_mutex_lock(mutex);

  return FIBER_SUCCESS;
}
