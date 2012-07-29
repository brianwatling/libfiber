#include "fiber_multi_channel.h"
#include "fiber_manager.h"
#include <stdio.h>
#include <inttypes.h>
#include <time.h>

#define NUM_THREADS 4

int64_t time_diff(const struct timespec* start, const struct timespec* end) {
    return (end->tv_sec * 1000000000LL + end->tv_nsec) - (start->tv_sec * 1000000000LL + start->tv_nsec);
}

void receiver(fiber_multi_channel_t* ch) {
    struct timespec last;
    clock_gettime(CLOCK_MONOTONIC, &last);
    int i;
    for(i = 0; i < 100000000; ++i) {
        intptr_t n = (intptr_t)fiber_multi_channel_receive(ch);
        if(n % 10000000 == 0) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            printf("Received 10000000 in %lf seconds\n", 0.000000001 * time_diff(&last, &now));
            last = now;
        }
    }
}

void sender(fiber_multi_channel_t* ch) {
    intptr_t n = 1;
    int i;
    for(i = 0; i < 100000000; ++i) {
        fiber_multi_channel_send(ch, (void*)n);
        n += 1;
    }
}

int main(int argc, char* argv[]) {
    fiber_manager_init(NUM_THREADS);

    fiber_multi_channel_t* ch1 = fiber_multi_channel_create(1000, 0);
    fiber_multi_channel_t* ch2 = fiber_multi_channel_create(1000, 0);
    fiber_t* r1 = fiber_create(1024, (fiber_run_function_t)&receiver, (void*)ch1);
    fiber_t* r2 = fiber_create(1024, (fiber_run_function_t)&receiver, (void*)ch2);
    fiber_create(1024, (fiber_run_function_t)&sender, (void*)ch2);
    sender(ch1);

    fiber_join(r1, NULL);
    fiber_join(r2, NULL);

    return 0;
}

