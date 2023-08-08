// SPDX-FileCopyrightText: 2012-2023 Brian Watling <brian@oxbo.dev>
// SPDX-License-Identifier: MIT

#include "fiber_barrier.h"
#include "fiber_manager.h"
#include "test_helper.h"

#define PER_FIBER_COUNT 10000
#define NUM_FIBERS 1000
#define NUM_THREADS 4

int volatile counter[NUM_FIBERS] = {};
int volatile winner = 0;

fiber_barrier_t barrier;

void* run_function(void* param) {
  intptr_t index = (intptr_t)param;
  int i;
  for (i = 0; i < PER_FIBER_COUNT; ++i) {
    test_assert(counter[index] == i);
    ++counter[index];
  }

  if (FIBER_BARRIER_SERIAL_FIBER == fiber_barrier_wait(&barrier)) {
    test_assert(__sync_bool_compare_and_swap(&winner, 0, 1));
  }

  for (i = 0; i < NUM_FIBERS; ++i) {
    test_assert(counter[i] == PER_FIBER_COUNT);
  }

  return NULL;
}

int main() {
  fiber_manager_init(NUM_THREADS);

  fiber_barrier_init(&barrier, NUM_FIBERS);

  fiber_t* fibers[NUM_FIBERS];
  intptr_t i;
  for (i = 1; i < NUM_FIBERS; ++i) {
    fibers[i] = fiber_create(20000, &run_function, (void*)i);
  }

  run_function(NULL);

  for (i = 1; i < NUM_FIBERS; ++i) {
    fiber_join(fibers[i], NULL);
  }

  test_assert(winner == 1);

  // do it all again - the barrier should be reusable
  winner = 0;
  memset((void*)counter, 0, sizeof(counter));
  for (i = 1; i < NUM_FIBERS; ++i) {
    fibers[i] = fiber_create(20000, &run_function, (void*)i);
  }

  run_function(NULL);

  for (i = 1; i < NUM_FIBERS; ++i) {
    fiber_join(fibers[i], NULL);
  }

  fiber_barrier_destroy(&barrier);

  fiber_manager_print_stats();
  fiber_shutdown();
  return 0;
}
