// SPDX-FileCopyrightText: 2012-2023 Brian Watling <brian@oxbo.dev>
// SPDX-License-Identifier: MIT

#include "fiber_manager.h"
#include "test_helper.h"

#define PER_FIBER_COUNT 100
#define NUM_FIBERS 100
#define NUM_THREADS 10
_Atomic int per_thread_count[NUM_THREADS];
_Atomic int switch_count = 0;

void* run_function(void* param) {
  fiber_manager_t* const original_manager = fiber_manager_get();
  int i;
  for (i = 0; i < PER_FIBER_COUNT; ++i) {
    fiber_manager_t* const current_manager = fiber_manager_get();
    if (current_manager != original_manager) {
      atomic_fetch_add(&switch_count, 1);
    }
    atomic_fetch_add(&per_thread_count[current_manager->id], 1);
    fiber_yield();
  }
  return NULL;
}

int main() {
  fiber_manager_init(NUM_THREADS);

  printf(
      "starting %d fibers with %d backing threads, running %d yields per "
      "fiber\n",
      NUM_FIBERS, NUM_THREADS, PER_FIBER_COUNT);
  fiber_t* fibers[NUM_FIBERS] = {};
  int i;
  for (i = 0; i < NUM_FIBERS; ++i) {
    fibers[i] = fiber_create(100000, &run_function, NULL);
    if (!fibers[i]) {
      printf("failed to create fiber!\n");
      return 1;
    }
  }

  for (i = 0; i < NUM_FIBERS; ++i) {
    fiber_join(fibers[i], NULL);
  }

  printf("SUCCESS\n");
  for (i = 0; i < NUM_THREADS; ++i) {
    printf("thread %d count: %d\n", i, per_thread_count[i]);
  }
  printf("switch_count: %d\n", switch_count);
  fflush(stdout);

  fiber_manager_print_stats();
  fiber_shutdown();
  return 0;
}
