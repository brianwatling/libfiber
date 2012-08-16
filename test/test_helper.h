#ifndef _FIBER_TEST_HELPER_H_
#define _FIBER_TEST_HELPER_H_

#include <stdio.h>
#include <inttypes.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fiber_manager.h>

#define test_assert(expr) \
    do { \
        if(!(expr)) { \
            fprintf(stderr, "%s:%d TEST FAILED: %s\n", __FILE__, __LINE__, #expr);\
            *(int*)0 = 0; \
        } \
    } while(0)

static inline void fiber_manager_print_stats()
{
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
           "\nlock_contention_count: %" PRIu64
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
           stats.event_wait_count,
           stats.lock_contention_count);
}

#endif

