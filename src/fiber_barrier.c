// SPDX-FileCopyrightText: 2012-2023 Brian Watling <brian@oxbo.dev>
// SPDX-License-Identifier: MIT

#include "fiber_barrier.h"

#include "fiber_manager.h"

int fiber_barrier_init(fiber_barrier_t* barrier, uint32_t count) {
  assert(barrier);
  assert(count > 0);
  barrier->count = count;
  barrier->counter = 0;
  if (!mpsc_fifo_init(&barrier->waiters)) {
    return FIBER_ERROR;
  }
  return FIBER_SUCCESS;
}

void fiber_barrier_destroy(fiber_barrier_t* barrier) {
  assert(barrier);
  mpsc_fifo_destroy(&barrier->waiters);
}

int fiber_barrier_wait(fiber_barrier_t* barrier) {
  assert(barrier);

  uint64_t const new_value = atomic_fetch_add(&barrier->counter, 1) + 1;
  if (new_value % barrier->count == 0) {
    fiber_manager_wake_from_mpsc_queue(fiber_manager_get(), &barrier->waiters,
                                       barrier->count - 1);
    return FIBER_BARRIER_SERIAL_FIBER;
  } else {
    fiber_manager_wait_in_mpsc_queue(fiber_manager_get(), &barrier->waiters);
    return 0;
  }
}
