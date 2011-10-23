#include "fiber_barrier.h"

int fiber_barrier_init(fiber_barrier_t* barrier, int count)
{
    assert(barrier);
    assert(count > 0);
    barrier->count = count;
    barrier->remaining = count;
    if(!mpsc_fifo_init(&barrier->waiters)) {
        return FIBER_ERROR;
    }
    return FIBER_SUCCESS;
}

void fiber_barrier_destroy(fiber_barrier_t* barrier)
{
    assert(barrier);
    mpsc_fifo_destroy(&barrier->waiters);
}

int fiber_barrier_wait(fiber_barrier_t* barrier)
{
    assert(barrier);

    int const new_value = __sync_sub_and_fetch(&barrier->remaining, 1);
    assert(new_value >= 0);
    if(new_value == 0) {
        fiber_manager_wake_from_queue(fiber_manager_get(), &barrier->waiters, barrier->count);
    } else {
        fiber_manager_wait_in_queue(fiber_manager_get(), &barrier->waiters);
    }
    return -new_value;//return remaining * -1 (or zero if this was the last call to barrier_wait() needed)
}

