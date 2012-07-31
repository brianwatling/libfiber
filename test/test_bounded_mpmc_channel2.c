#include "fiber_multi_channel.h"
#include "fiber_manager.h"
#include <stdio.h>
#include <inttypes.h>
#include <time.h>

#define NUM_THREADS 4

int send_count = 100000000;

int64_t time_diff(const struct timespec* start, const struct timespec* end) {
    return (end->tv_sec * 1000000000LL + end->tv_nsec) - (start->tv_sec * 1000000000LL + start->tv_nsec);
}

void receiver(fiber_multi_channel_t* ch) {
    fiber_t* this_fiber = fiber_manager_get()->current_fiber;
    struct timespec last;
    clock_gettime(CLOCK_MONOTONIC, &last);
    int i;
    for(i = 0; i < send_count; ++i) {
        intptr_t n = (intptr_t)fiber_multi_channel_receive(ch);
        if(n % 10000000 == 0) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            printf("%p Received 10000000 in %lf seconds\n", this_fiber, 0.000000001 * time_diff(&last, &now));
            last = now;
        }
    }
}

void sender(fiber_multi_channel_t* ch) {
    intptr_t n = 1;
    int i;
    for(i = 0; i < send_count; ++i) {
        fiber_multi_channel_send(ch, (void*)n);
        n += 1;
    }
}

int main(int argc, char* argv[]) {
    fiber_manager_init(NUM_THREADS);

    int count = 2;
    if(argc > 1) {
        count = atoi(argv[1]);
    }
    if(argc > 2) {
        send_count = atoi(argv[2]);
    }

    fiber_multi_channel_t* ch1 = fiber_multi_channel_create(10, 0);

    fiber_t** fibers = calloc(count, sizeof(fiber_t*));
    int i;
    for(i = 0; i < count; ++i) {
        fibers[i] = fiber_create(1024, (fiber_run_function_t)&receiver, (void*)ch1);
        fiber_create(1024, (fiber_run_function_t)&sender, (void*)ch1);
    }

    for(i = 0; i < count; ++i) {
        fiber_join(fibers[i], NULL);
    }

    fiber_manager_stats_t stats;
    fiber_manager_all_stats(&stats);
    printf("yield_count: %" PRIu64
           "\nsteal_count: %" PRIu64
           "\nfailed_steal_count: %" PRIu64
           "\nspin_count: %" PRIu64
           "\nsignal_spin_count: %" PRIu64
           "\nmulti_signal_spin_count: %" PRIu64
           "\nwake_mpsc_spin_count: %" PRIu64
           "\nwake_mpmc_spin_count: %" PRIu64
           "\npoll_count: %" PRIu64
           "\nevent_wait_count: %" PRIu64
           "\n",
           stats.yield_count,
           stats.steal_count,
           stats.failed_steal_count,
           stats.spin_count,
           stats.signal_spin_count,
           stats.multi_signal_spin_count,
           stats.wake_mpsc_spin_count,
           stats.wake_mpmc_spin_count,
           stats.poll_count,
           stats.event_wait_count);

    return 0;
}

