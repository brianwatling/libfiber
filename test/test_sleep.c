// SPDX-FileCopyrightText: 2012-2023 Brian Watling <brian@oxbo.dev>
// SPDX-License-Identifier: MIT

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "fiber_event.h"
#include "fiber_io.h"
#include "fiber_manager.h"
#include "test_helper.h"

#define NUM_THREADS 2
#define NUM_FIBERS 1000

_Atomic int done_count = 0;

void* sleep_function(void* param) {
  int i;
  for (i = 0; i < 100; ++i) {
    usleep(rand() % 1000 + 1000);
  }
  atomic_fetch_add(&done_count, 1);
  return NULL;
}

int main() {
  fiber_manager_init(NUM_THREADS);

  fiber_t* fibers[NUM_FIBERS];
  int i;
  for (i = 0; i < NUM_FIBERS; ++i) {
    fibers[i] = fiber_create(100000, &sleep_function, NULL);
  }

  for (i = 0; i < NUM_FIBERS; ++i) {
    fiber_join(fibers[i], NULL);
  }

  fiber_manager_print_stats();
  fiber_shutdown();
  return 0;
}
