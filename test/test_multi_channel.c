// SPDX-FileCopyrightText: 2012-2023 Brian Watling <brian@oxbo.dev>
// SPDX-License-Identifier: MIT

#include "fiber_channel.h"
#include "fiber_manager.h"
#include "test_helper.h"

fiber_bounded_channel_t* bounded_channel = NULL;
fiber_unbounded_channel_t unbounded_channel;
#define PER_FIBER_COUNT 1000
#define NUM_FIBERS 100
#define NUM_THREADS 2

int results[PER_FIBER_COUNT] = {};

void* send_function(void* param) {
  intptr_t i;
  for (i = 1; i < PER_FIBER_COUNT + 1; ++i) {
    fiber_bounded_channel_send(bounded_channel, (void*)i);
    fiber_unbounded_channel_message_t* node = malloc(sizeof(*node));
    test_assert(node);
    node->data = (void*)i;
    fiber_unbounded_channel_send(&unbounded_channel, node);
  }
  return NULL;
}

int main(int argc, char* argv[]) {
  fiber_manager_init(NUM_THREADS);

  fiber_signal_t signal;
  fiber_signal_init(&signal);
  // these channels share a signal; this allows a fiber to wait on both at once
  bounded_channel = fiber_bounded_channel_create(10, argc > 1 ? NULL : &signal);
  fiber_unbounded_channel_init(&unbounded_channel, argc > 1 ? NULL : &signal);

  fiber_t* send_fibers[NUM_FIBERS];
  int i;
  for (i = 0; i < NUM_FIBERS; ++i) {
    send_fibers[i] = fiber_create(20000, &send_function, NULL);
  }

  for (i = 0; i < 2 * NUM_FIBERS * PER_FIBER_COUNT; ++i) {
    intptr_t result;
    while (1) {
      if (fiber_bounded_channel_try_receive(bounded_channel, (void**)&result)) {
        break;
      }
      fiber_unbounded_channel_message_t* const node =
          fiber_unbounded_channel_receive(&unbounded_channel);
      if (node) {
        result = (intptr_t)node->data;
        free(node);
        break;
      }
      if (argc <= 1) {
        fiber_signal_wait(&signal);
      }
    }

    results[result - 1] += 1;
  }

  for (i = 0; i < NUM_FIBERS; ++i) {
    fiber_join(send_fibers[i], NULL);
  }

  for (i = 0; i < PER_FIBER_COUNT; ++i) {
    test_assert(results[i] == 2 * NUM_FIBERS);
  }
  fiber_bounded_channel_destroy(bounded_channel);
  fiber_unbounded_channel_destroy(&unbounded_channel);
  fiber_signal_destroy(&signal);

  fiber_manager_print_stats();
  fiber_shutdown();
  return 0;
}
