#include "fiber_semaphore.h"
#include "fiber_manager.h"

int fiber_semaphore_init(fiber_semaphore_t* semaphore, int value)
{
    assert(semaphore);
    semaphore->counter = value;
    if(!mpsc_fifo_init(&semaphore->waiters)) {
        return FIBER_ERROR;
    }
    write_barrier();
    return FIBER_SUCCESS;
}

int fiber_semaphore_destroy(fiber_semaphore_t* semaphore)
{
    assert(semaphore);
    semaphore->counter = 0;
    mpsc_fifo_destroy(&semaphore->waiters);
    return FIBER_SUCCESS;
}

int fiber_semaphore_wait(fiber_semaphore_t* semaphore)
{
    assert(semaphore);

    const int val = __sync_sub_and_fetch(&semaphore->counter, 1);
    if(val >= 0) {
        //we just got in, there was no contention
        load_load_barrier();
        return FIBER_SUCCESS;
    }

    //we didn't get in, we'll wait
    fiber_manager_wait_in_queue(fiber_manager_get(), &semaphore->waiters);
    load_load_barrier();

    return FIBER_SUCCESS;
}

int fiber_semaphore_trywait(fiber_semaphore_t* semaphore)
{
    assert(semaphore);

    int counter;
    while((counter = semaphore->counter) > 0) {
        if(__sync_bool_compare_and_swap(&semaphore->counter, counter, counter - 1)) {
            load_load_barrier();
            return FIBER_SUCCESS;
        }
    }
    return FIBER_ERROR;
}

//returns 1 if another fiber was woken after releasing the semaphore, 0 otherwise
int fiber_semaphore_post_internal(fiber_semaphore_t* semaphore)
{
    assert(semaphore);

    store_load_barrier();//flush this fiber's writes

    int prev_counter;
    do {
        while((prev_counter = semaphore->counter) < 0) {
            //another fiber is waiting; attempt to schedule it to take this fiber's place
            //TODO: this is an mpSC queue; can't have many fibers trying to pop!
            if(fiber_manager_wake_from_queue(fiber_manager_get(), &semaphore->waiters, 0)) {
                __sync_add_and_fetch(&semaphore->counter, 1);
                return 1;
            }
        }
    } while(!__sync_bool_compare_and_swap(&semaphore->counter, prev_counter, prev_counter + 1));

    return 0;
}

int fiber_semaphore_post(fiber_semaphore_t* semaphore)
{
    const int had_waiters = fiber_semaphore_post_internal(semaphore);
    if(had_waiters) {
        //the semaphore was contended - be nice and let the waiter run
        fiber_yield();
    }
    return FIBER_SUCCESS;
}

int fiber_semaphore_getvalue(fiber_semaphore_t* semaphore)
{
    assert(semaphore);
    return semaphore->counter;
}

