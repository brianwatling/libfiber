// SPDX-FileCopyrightText: 2012-2023 Brian Watling <brian@oxbo.dev>
// SPDX-License-Identifier: MIT

#include "fiber_cond.h"
#include "fiber_manager.h"
#include "test_helper.h"

int volatile counter = 1;
fiber_mutex_t mutex;
fiber_cond_t cond;
#define PER_FIBER_COUNT 1000
#define NUM_FIBERS 100
#define NUM_THREADS 2

void* run_function(void* param) {
  intptr_t myNum = (intptr_t)param;
  int i;
  for (i = 0; i < PER_FIBER_COUNT; ++i) {
    fiber_mutex_lock(&mutex);
    while (counter < (myNum + i * NUM_FIBERS)) {
      fiber_cond_wait(&cond, &mutex);
    }
    test_assert(counter == (myNum + i * NUM_FIBERS));
    ++counter;
    fiber_mutex_unlock(&mutex);
    fiber_cond_broadcast(&cond);
  }
  return NULL;
}

void* run_single(void* param) {
  fiber_mutex_lock(&mutex);
  fiber_cond_signal(&cond);
  fiber_cond_wait(&cond, &mutex);
  fiber_mutex_unlock(&mutex);
  return NULL;
}

int main() {
  fiber_manager_init(NUM_THREADS);

  fiber_mutex_init(&mutex);
  fiber_cond_init(&cond);

  fiber_t* fibers[NUM_FIBERS];
  intptr_t i;
  for (i = 1; i <= NUM_FIBERS; ++i) {
    fibers[i - 1] = fiber_create(20000, &run_function, (void*)i);
  }

  for (i = 1; i <= NUM_FIBERS; ++i) {
    fiber_join(fibers[i - 1], NULL);
  }

  test_assert(counter == (NUM_FIBERS * PER_FIBER_COUNT + 1));

  fiber_t* single = fiber_create(20000, &run_single, NULL);
  fiber_mutex_lock(&mutex);
  fiber_cond_wait(&cond, &mutex);
  fiber_cond_signal(&cond);
  fiber_mutex_unlock(&mutex);
  fiber_join(single, NULL);

  fiber_cond_destroy(&cond);

  fiber_manager_print_stats();
  fiber_shutdown();
  return 0;
}
