// SPDX-FileCopyrightText: 2012-2023 Brian Watling <brian@oxbo.dev>
// SPDX-License-Identifier: MIT

#include "fiber_barrier.h"
#include "fiber_manager.h"
#include "fiber_rwlock.h"
#include "test_helper.h"

#define PER_FIBER_COUNT 10000
#define NUM_FIBERS 100
#define NUM_THREADS 2

fiber_rwlock_t mutex;
volatile int counter = 0;
#define READING 1
#define WRITING -1
#define NONE 0
int state = NONE;
fiber_barrier_t barrier;
volatile int try_wr = 0;
volatile int try_rd = 0;
volatile int count_rd = 0;
volatile int count_wr = 0;

void* run_function(void* param) {
  fiber_barrier_wait(&barrier);
  int i;
  for (i = 0; i < PER_FIBER_COUNT || (!try_wr || !try_rd); ++i) {
    if (i % 10 == 0) {
      fiber_rwlock_wrlock(&mutex);
      __sync_fetch_and_add(&count_wr, 1);
      int old_state = atomic_exchange_int(&state, WRITING);
      test_assert(old_state == NONE);
      old_state = atomic_exchange_int(&state, NONE);
      test_assert(old_state == WRITING);
      fiber_rwlock_wrunlock(&mutex);
    } else if (i % 10 == 1 && fiber_rwlock_trywrlock(&mutex)) {
      __sync_fetch_and_add(&try_wr, 1);
      int old_state = atomic_exchange_int(&state, WRITING);
      test_assert(old_state == NONE);
      old_state = atomic_exchange_int(&state, NONE);
      test_assert(old_state == WRITING);
      fiber_rwlock_wrunlock(&mutex);
    } else if (i % 10 == 2 && fiber_rwlock_tryrdlock(&mutex)) {
      __sync_fetch_and_add(&try_rd, 1);
      int old_state = atomic_exchange_int(&state, READING);
      test_assert(old_state == NONE || old_state == READING);
      old_state = atomic_exchange_int(&state, NONE);
      test_assert(old_state == NONE || old_state == READING);
      fiber_rwlock_rdunlock(&mutex);
    } else {
      fiber_rwlock_rdlock(&mutex);
      __sync_fetch_and_add(&count_rd, 1);
      int old_state = atomic_exchange_int(&state, READING);
      test_assert(old_state == NONE || old_state == READING);
      old_state = atomic_exchange_int(&state, NONE);
      test_assert(old_state == NONE || old_state == READING);
      fiber_rwlock_rdunlock(&mutex);
    }
  }
  return NULL;
}

int main() {
  fiber_manager_init(NUM_THREADS);

  fiber_rwlock_init(&mutex);
  fiber_barrier_init(&barrier, NUM_FIBERS);

  fiber_t* fibers[NUM_FIBERS];
  int i;
  for (i = 0; i < NUM_FIBERS; ++i) {
    fibers[i] = fiber_create(20000, &run_function, NULL);
  }

  for (i = 0; i < NUM_FIBERS; ++i) {
    fiber_join(fibers[i], NULL);
  }

  fiber_barrier_destroy(&barrier);
  fiber_rwlock_destroy(&mutex);

  printf("try_rd %d try_wr %d count_rd %d count_wr %d\n", try_rd, try_wr,
         count_rd, count_wr);

  fiber_manager_print_stats();
  fiber_shutdown();
  return 0;
}
