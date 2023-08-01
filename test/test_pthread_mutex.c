// SPDX-FileCopyrightText: 2012-2023 Brian Watling <brian@oxbo.dev>
// SPDX-License-Identifier: MIT

#include <pthread.h>

#include "test_helper.h"

int volatile counter = 0;
pthread_mutex_t mutex;
#define PER_FIBER_COUNT 100000
#define NUM_FIBERS 100
#define NUM_THREADS 4

void* run_function(void* param) {
  int i;
  for (i = 0; i < PER_FIBER_COUNT; ++i) {
    pthread_mutex_lock(&mutex);
    ++counter;
    pthread_mutex_unlock(&mutex);
  }
  return NULL;
}

int main() {
  // fiber_manager_init(NUM_THREADS);

  pthread_mutex_init(&mutex, NULL);

  pthread_t fibers[NUM_FIBERS];
  int i;
  for (i = 0; i < NUM_FIBERS; ++i) {
    pthread_create(&fibers[i], NULL, &run_function, NULL);
  }

  for (i = 0; i < NUM_FIBERS; ++i) {
    pthread_join(fibers[i], NULL);
  }

  test_assert(counter == NUM_FIBERS * PER_FIBER_COUNT);

  return 0;
}
