#include "fiber_cond.h"
#include "fiber_manager.h"

int fiber_cond_init(fiber_cond_t* cond)
{
    assert(cond);
    memset(cond, 0, sizeof(*cond));
    mpmc_queue_init(&cond->waiter_queue);
    return fiber_mutex_init(&cond->internal_mutex);
}

void fiber_cond_destroy(fiber_cond_t* cond)
{
    assert(cond);
    fiber_mutex_destroy(&cond->internal_mutex);
    memset(cond, 0, sizeof(*cond));
}

int fiber_cond_signal(fiber_cond_t* cond)
{
    assert(cond);

    fiber_mutex_lock(&cond->internal_mutex);
    if(cond->popped_waiters) {
        fiber_t* const to_wake = (fiber_t*)cond->popped_waiters->data;
        cond->popped_waiters = cond->popped_waiters->next;
    } else if(cond->waiter_count > 0) {
        __sync_fetch_and_sub(&cond->waiter_count, 1);
        fiber_manager_wake_from_mpmc_queue(fiber_manager_get(), &cond->waiters, 1);
    }
    fiber_mutex_unlock(&cond->internal_mutex);

    return FIBER_SUCCESS;
}

int fiber_cond_broadcast(fiber_cond_t* cond)
{
    assert(cond);

    fiber_mutex_lock(&cond->internal_mutex);
    int original = cond->waiter_count;
    while(original > 0) {
        const int latest = __sync_val_compare_and_swap(&cond->waiter_count, original, 0);
        if(latest == original) {
            fiber_manager_wake_from_mpmc_queue(fiber_manager_get(), &cond->waiter_queue, original);
            break;
        }
        original = latest;
    }
    fiber_mutex_unlock(&cond->internal_mutex);

    return FIBER_SUCCESS;
}

int fiber_cond_timedwait(fiber_cond_t* cond, fiber_mutex_t* mutex, const struct timespec* abstime)
{
    //TODO: implement this. not sure how I want to do it yet.
    return FIBER_ERROR;
}

int fiber_cond_wait(fiber_cond_t* cond, fiber_mutex_t * mutex)
{
    assert(cond);
    assert(mutex);
    assert(!cond->caller_mutex || cond->caller_mutex == mutex);

    cond->caller_mutex = mutex;

    //incremenet the waiter count before unlocking the mutex - this allows a
    //signaller to wait until we've entered the queue before signalling us (ie
    //if he grabs the mutex after the unlock but before the wait_in_queue)
    __sync_fetch_and_add(&cond->waiter_count, 1);

    fiber_mutex_unlock(mutex);
    fiber_manager_wait_in_mpmc_queue(fiber_manager_get(), &cond->waiter_queue);
    fiber_mutex_lock(mutex);

    return FIBER_SUCCESS;
}

