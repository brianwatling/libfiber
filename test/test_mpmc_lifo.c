// SPDX-FileCopyrightText: 2012-2023 Brian Watling <brian@oxbo.dev>
// SPDX-License-Identifier: MIT

#include <mpmc_lifo.h>
#include <pthread.h>
#include <stdint.h>
#include <unistd.h>

#include "test_helper.h"

#define NUM_THREADS 4
#define PER_THREAD_COUNT 250000

int pushes[NUM_THREADS] = {};
int pops[NUM_THREADS] = {};
int volatile done[NUM_THREADS] = {};
pthread_barrier_t barrier;

mpmc_lifo_t the_q;

void* push_func(void* p) {
  pthread_barrier_wait(&barrier);
  intptr_t thr = (intptr_t)p;
  intptr_t i = 0;
  while (!done[thr]) {
    ++i;
    mpmc_lifo_node_t* n = malloc(sizeof(mpmc_lifo_node_t));
    n->data = (void*)i;
    mpmc_lifo_push(&the_q, n);
  }
  return NULL;
}

void* pop_func(void* p) {
  pthread_barrier_wait(&barrier);
  intptr_t thr = (intptr_t)p;
  intptr_t last = 0;
  (void)last;
  intptr_t counter = 0;
  while (!done[thr]) {
    mpmc_lifo_node_t* node = mpmc_lifo_pop(&the_q);
    if (!node) {
      usleep(1);
    } else {
      intptr_t i = (intptr_t)node->data;
      if (NUM_THREADS == 1) {
        assert(i > last);
        last = i;
      }
      ++counter;
      if (counter > PER_THREAD_COUNT) {
        done[thr] = 1;
      }
      free(node);
    }
  }
  return NULL;
}

int main() {
  pthread_barrier_init(&barrier, NULL, 2 * NUM_THREADS);
  mpmc_lifo_init(&the_q);

  pthread_t reader[NUM_THREADS];
  pthread_t writer[NUM_THREADS];
  int i;
  for (i = 0; i < NUM_THREADS; ++i) {
    pthread_create(&reader[i], NULL, &pop_func, (void*)(intptr_t)i);
  }
  for (i = 1; i < NUM_THREADS; ++i) {
    pthread_create(&writer[i], NULL, &push_func, (void*)(intptr_t)i);
  }

  push_func(0);

  for (i = 1; i < NUM_THREADS; ++i) {
    pthread_join(reader[i], NULL);
  }
  for (i = 1; i < NUM_THREADS; ++i) {
    pthread_join(writer[i], NULL);
  }

  return 0;
}
