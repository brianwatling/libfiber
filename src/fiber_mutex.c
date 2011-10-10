#include "fiber_mutex.h"
#include "fiber_manager.h"

int fiber_mutex_init(fiber_mutex_t* mutex)
{
    assert(mutex);
    mutex->counter = 1;
    mutex->owner = NULL;
    mutex->waiters = mpsc_fifo_create(fiber_manager_get_kernel_thread_count());
    if(!mutex->waiters) {
        return FIBER_ERROR;
    }
    return FIBER_SUCCESS;
}

int fiber_mutex_destroy(fiber_mutex_t* mutex)
{
    assert(mutex);
    mutex->counter = 1;
    mpsc_fifo_destroy(mutex->waiters);
    mutex->waiters = NULL;
    return FIBER_SUCCESS;
}

int fiber_mutex_lock(fiber_mutex_t* mutex)
{
    assert(mutex);

    const int val = __sync_sub_and_fetch(&mutex->counter, 1);
    if(val == 0) {
        //we just got the lock, there was no contention
        assert(mutex->owner == NULL);
        mutex->owner = fiber_manager_get()->current_fiber;
        return FIBER_SUCCESS;
    }

    //we failed to acquire the lock (there's contention). we'll wait.
    fiber_manager_t* const manager = fiber_manager_get();
    fiber_t* const this_fiber = manager->current_fiber;
    this_fiber->state = FIBER_STATE_WAITING;
    assert(this_fiber->spsc_node);
    this_fiber->spsc_node->data = this_fiber;
    write_barrier();
    mpsc_fifo_push(mutex->waiters, manager->id, this_fiber->spsc_node);
    this_fiber->spsc_node = NULL;
    fiber_manager_yield(manager);

    assert(mutex->owner == NULL);
    mutex->owner = fiber_manager_get()->current_fiber;

    return FIBER_SUCCESS;
}

int fiber_mutex_trylock(fiber_mutex_t* mutex)
{
    assert(mutex);

    if(__sync_bool_compare_and_swap(&mutex->counter, 1, 0)) {
        //we just got the lock, there was no contention
        assert(mutex->owner == NULL);
        mutex->owner = fiber_manager_get()->current_fiber;
        return FIBER_SUCCESS;
    }
    return FIBER_ERROR;
}

int fiber_mutex_unlock(fiber_mutex_t* mutex)
{
    assert(mutex);
    assert(mutex->owner == fiber_manager_get()->current_fiber);
    mutex->owner = NULL;

    store_load_barrier();//flush this fiber's writes

    if(__sync_bool_compare_and_swap(&mutex->counter, 0, 1)) {
        //there's no other fibers waiting
        return FIBER_SUCCESS;
    }

    //some other fiber is waiting - pop and schedule another fiber

    spsc_node_t* out = NULL;
    do {
        if(!mpsc_fifo_pop(mutex->waiters, &out)) {
            fiber_yield();
        }
    } while(!out);

    __sync_add_and_fetch(&mutex->counter, 1);

    fiber_t* const to_schedule = (fiber_t*)out->data;
    to_schedule->spsc_node = out;
    assert(to_schedule->state == FIBER_STATE_WAITING);
    to_schedule->state = FIBER_STATE_READY;
    fiber_manager_schedule(fiber_manager_get(), to_schedule);

    return FIBER_SUCCESS;
}

