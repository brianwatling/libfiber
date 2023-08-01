// SPDX-FileCopyrightText: 2012-2023 Brian Watling <brian@oxbo.dev>
// SPDX-License-Identifier: MIT

#include "fiber_manager.h"
#include "fiber_spinlock.h"
#include "test_helper.h"

int volatile counter = 0;
fiber_spinlock_t mutex;
#define PER_FIBER_COUNT 1000000
#define NUM_FIBERS 100
#define NUM_THREADS 2

void* run_function(void* param) {
  int i;
  for (i = 0; i < PER_FIBER_COUNT; ++i) {
    fiber_spinlock_lock(&mutex);
    ++counter;
    fiber_spinlock_unlock(&mutex);
  }
  return NULL;
}

int main() {
  fiber_manager_init(NUM_THREADS);

  fiber_spinlock_init(&mutex);

  fiber_t* fibers[NUM_FIBERS];
  int i;
  for (i = 0; i < NUM_FIBERS; ++i) {
    fibers[i] = fiber_create(20000, &run_function, NULL);
  }

  for (i = 0; i < NUM_FIBERS; ++i) {
    fiber_join(fibers[i], NULL);
  }

  test_assert(counter == NUM_FIBERS * PER_FIBER_COUNT);
  test_assert(fiber_spinlock_trylock(&mutex));
  test_assert(!fiber_spinlock_trylock(&mutex));
  fiber_spinlock_unlock(&mutex);
  fiber_spinlock_destroy(&mutex);

  fiber_manager_print_stats();
  return 0;
}
