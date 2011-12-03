#include "fiber_spinlock.h"
#include "fiber.h"
#include "sched.h"

int fiber_spinlock_init(fiber_spinlock_t* spinlock)
{
    assert(spinlock);
    spinlock->state = 0;
    return FIBER_SUCCESS;
}

int fiber_spinlock_destroy(fiber_spinlock_t* spinlock)
{
    assert(spinlock);
    return FIBER_SUCCESS;
}

int fiber_spinlock_lock(fiber_spinlock_t* spinlock)
{
    assert(spinlock);
    unsigned int counter = 0;
    while(!__sync_bool_compare_and_swap(&spinlock->state, 0, 1)) {
        while(spinlock->state) {
            ++counter;
            if(counter > 128) {
                sched_yield();
                counter = 0;
            }
        }
    }
    load_load_barrier();
    return FIBER_SUCCESS;
}

int fiber_spinlock_trylock(fiber_spinlock_t* spinlock, size_t tries)
{
    assert(spinlock);
    while(!__sync_bool_compare_and_swap(&spinlock->state, 0, 1)) {
        if(tries == 0) {
            return FIBER_ERROR;
        }
        --tries;
        while(spinlock->state && tries > 0) {
            --tries;
        }
    }
    load_load_barrier();
    return FIBER_SUCCESS;
}

int fiber_spinlock_unlock(fiber_spinlock_t* spinlock)
{
    assert(spinlock);
    store_load_barrier();//flush this fiber's writes
    spinlock->state = 0;
    return FIBER_SUCCESS;
}

