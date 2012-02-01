#include "fiber_mutex.h"
#include "fiber_manager.h"

int fiber_mutex_init(fiber_mutex_t* mutex)
{
    assert(mutex);
    mutex->counter = 1;
    if(!mpsc_fifo_init(&mutex->waiters)) {
        return FIBER_ERROR;
    }
    write_barrier();
    return FIBER_SUCCESS;
}

int fiber_mutex_destroy(fiber_mutex_t* mutex)
{
    assert(mutex);
    mutex->counter = 1;
    mpsc_fifo_destroy(&mutex->waiters);
    return FIBER_SUCCESS;
}

int fiber_mutex_lock(fiber_mutex_t* mutex)
{
    assert(mutex);

    const int val = __sync_sub_and_fetch(&mutex->counter, 1);
    if(val == 0) {
        //we just got the lock, there was no contention
        load_load_barrier();
        return FIBER_SUCCESS;
    }

    //we failed to acquire the lock (there's contention). we'll wait.
    fiber_manager_wait_in_queue(fiber_manager_get(), &mutex->waiters);
    load_load_barrier();

    return FIBER_SUCCESS;
}

int fiber_mutex_trylock(fiber_mutex_t* mutex)
{
    assert(mutex);

    if(__sync_bool_compare_and_swap(&mutex->counter, 1, 0)) {
        //we just got the lock, there was no contention
        load_load_barrier();
        return FIBER_SUCCESS;
    }
    return FIBER_ERROR;
}

int fiber_mutex_unlock_internal(fiber_mutex_t* mutex)
{
    assert(mutex);

    store_load_barrier();//flush this fiber's writes

    if(__sync_bool_compare_and_swap(&mutex->counter, 0, 1)) {
        //there's no other fibers waiting
        return 0;
    }

    //some other fiber is waiting - pop and schedule another fiber
    fiber_manager_wake_from_queue(fiber_manager_get(), &mutex->waiters, 1);
    //now we can unlock the mutex - before this we hold it since the mutex double-purposes as a lock on the consumer side of the fifo
    __sync_add_and_fetch(&mutex->counter, 1);
    return 1;
}

int fiber_mutex_unlock(fiber_mutex_t* mutex)
{
    const int contended = fiber_mutex_unlock_internal(mutex);
    if(contended) {
        //the lock was contended - be nice and let the waiter run
        fiber_yield();
    }

    return FIBER_SUCCESS;
}

