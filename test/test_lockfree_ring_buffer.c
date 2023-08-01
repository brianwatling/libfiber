// SPDX-FileCopyrightText: 2012-2023 Brian Watling <brian@oxbo.dev>
// SPDX-License-Identifier: MIT

#include <pthread.h>

#include "lockfree_ring_buffer.h"
#include "test_helper.h"

#define PER_THREAD_COUNT 10000000
#define NUM_THREADS 4

lockfree_ring_buffer_t* rb;
char counters[PER_THREAD_COUNT] = {};
pthread_barrier_t barrier;

void* run_function(void* param) {
  pthread_barrier_wait(&barrier);
  intptr_t i;
  for (i = 1; i <= PER_THREAD_COUNT; ++i) {
    lockfree_ring_buffer_push(rb, (void*)i);
    const size_t size = lockfree_ring_buffer_size(rb);
    test_assert(size <= 128);
    intptr_t j = (intptr_t)lockfree_ring_buffer_pop(rb);
    test_assert(j > 0 && j <= PER_THREAD_COUNT);
    __sync_add_and_fetch(&counters[j - 1], 1);
  }
  return NULL;
}

int main() {
  rb = lockfree_ring_buffer_create(7);
  pthread_barrier_init(&barrier, NULL, NUM_THREADS);

  pthread_t threads[NUM_THREADS];
  int i;
  for (i = 0; i < NUM_THREADS; ++i) {
    pthread_create(&threads[i], NULL, &run_function, NULL);
  }

  for (i = 0; i < NUM_THREADS; ++i) {
    pthread_join(threads[i], NULL);
  }

  lockfree_ring_buffer_destroy(rb);

  for (i = 0; i < PER_THREAD_COUNT; ++i) {
    test_assert(counters[i] == NUM_THREADS);
  }

  return 0;
}
