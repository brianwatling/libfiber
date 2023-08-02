// SPDX-FileCopyrightText: 2012-2023 Brian Watling <brian@oxbo.dev>
// SPDX-License-Identifier: MIT

#include <time.h>

#include "fiber_manager.h"
#include "fiber_multi_channel.h"
#include "test_helper.h"

#define NUM_THREADS (4)
int send_count = 100000;

int64_t time_diff(const struct timespec* start, const struct timespec* end) {
  return (end->tv_sec * 1000000000LL + end->tv_nsec) -
         (start->tv_sec * 1000000000LL + start->tv_nsec);
}

void receiver(fiber_multi_channel_t* ch) {
  struct timespec last;
  clock_gettime(CLOCK_MONOTONIC, &last);
  int i;
  for (i = 0; i < send_count; ++i) {
    intptr_t n = (intptr_t)fiber_multi_channel_receive(ch);
    if (n % send_count == 0) {
      struct timespec now;
      clock_gettime(CLOCK_MONOTONIC, &now);
      printf("Received 10000000 in %lf seconds\n",
             0.000000001 * time_diff(&last, &now));
      last = now;
    }
  }
}

void sender(fiber_multi_channel_t* ch) {
  intptr_t n = 0;
  int i;
  for (i = 0; i < send_count; ++i) {
    fiber_multi_channel_send(ch, (void*)n);
    n += 1;
  }
}

int main(int argc, char* argv[]) {
  if (argc > 1) {
    send_count = atoi(argv[1]);
  }

  fiber_manager_init(NUM_THREADS);

  fiber_multi_channel_t* ch1 = fiber_multi_channel_create(10);
  fiber_multi_channel_t* ch2 = fiber_multi_channel_create(10);
  fiber_t* r1 =
      fiber_create(10240, (fiber_run_function_t)&receiver, (void*)ch1);
  fiber_t* r2 =
      fiber_create(10240, (fiber_run_function_t)&receiver, (void*)ch2);
  fiber_create(10240, (fiber_run_function_t)&sender, (void*)ch2);
  sender(ch1);

  fiber_join(r1, NULL);
  fiber_join(r2, NULL);

  fiber_multi_channel_destroy(ch1);
  fiber_multi_channel_destroy(ch2);
  fiber_manager_print_stats();
  fiber_shutdown();
  return 0;
}
