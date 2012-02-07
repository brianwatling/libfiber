#include "fiber_rwlock.h"
#include "fiber_manager.h"

int fiber_rwlock_init(fiber_rwlock_t* rwlock)
{
    assert(rwlock);
    if(!fiber_spinlock_init(&rwlock->lock)) {
        return FIBER_ERROR;
    }
    if(!mpsc_fifo_init(&rwlock->waiters)) {
        fiber_spinlock_destroy(&rwlock->lock);
        return FIBER_ERROR;
    }
    rwlock->counter = 0;//positive number indicates how many readers share the lock, -1 indicates a writer owns it
    rwlock->waiting_writers = 0;
    rwlock->waiting_readers = 0;
    write_barrier();
    return FIBER_SUCCESS;
}

void fiber_rwlock_destroy(fiber_rwlock_t* rwlock)
{
    mpsc_fifo_destroy(&rwlock->waiters);
    fiber_spinlock_destroy(&rwlock->lock);
}

#define FIBER_RWLOCK_SCRATCH_READ ((void*)1)
#define FIBER_RWLOCK_SCRATCH_WRITE ((void*)2)

int fiber_rwlock_rdlock(fiber_rwlock_t* rwlock)
{
    assert(rwlock);

    fiber_spinlock_lock(&rwlock->lock);
    if(rwlock->waiting_writers || rwlock->counter < 0) {
        rwlock->waiting_readers += 1;
        fiber_spinlock_unlock(&rwlock->lock);

        fiber_manager_t* const manager = fiber_manager_get();
        manager->current_fiber->scratch = FIBER_RWLOCK_SCRATCH_READ;
        fiber_manager_wait_in_mpsc_queue(manager, &rwlock->waiters);
    } else {
        rwlock->counter += 1;
        fiber_spinlock_unlock(&rwlock->lock);
    }

    load_load_barrier();
    return FIBER_SUCCESS;
}

int fiber_rwlock_wrlock(fiber_rwlock_t* rwlock)
{
    assert(rwlock);

    fiber_spinlock_lock(&rwlock->lock);
    if(rwlock->waiting_readers || rwlock->counter != 0) {
        rwlock->waiting_writers += 1;
        fiber_spinlock_unlock(&rwlock->lock);

        fiber_manager_t* const manager = fiber_manager_get();
        manager->current_fiber->scratch = FIBER_RWLOCK_SCRATCH_WRITE;
        fiber_manager_wait_in_mpsc_queue(manager, &rwlock->waiters);
    } else {
        rwlock->counter = -1;
        fiber_spinlock_unlock(&rwlock->lock);
    }

    load_load_barrier();
    return FIBER_SUCCESS;
}

int fiber_rwlock_tryrdlock(fiber_rwlock_t* rwlock)
{
    assert(rwlock);

    fiber_spinlock_lock(&rwlock->lock);
    if(rwlock->counter >= 0 && !rwlock->waiting_writers) {
        assert(!rwlock->waiting_readers);
        rwlock->counter += 1;
        fiber_spinlock_unlock(&rwlock->lock);
        return FIBER_SUCCESS;
    }
    fiber_spinlock_unlock(&rwlock->lock);
    return FIBER_ERROR;
}

int fiber_rwlock_trywrlock(fiber_rwlock_t* rwlock)
{
    assert(rwlock);

    fiber_spinlock_lock(&rwlock->lock);
    if(!rwlock->counter && !rwlock->waiting_readers && !rwlock->waiting_writers) {
        rwlock->counter = -1;
        fiber_spinlock_unlock(&rwlock->lock);
        return FIBER_SUCCESS;
    }
    fiber_spinlock_unlock(&rwlock->lock);
    return FIBER_ERROR;
}

static void fiber_rwlock_wake_waiters(fiber_rwlock_t* rwlock)
{
    assert(rwlock->waiting_readers >= 0);
    assert(rwlock->waiting_writers >= 0);

    void* data = NULL;
    while(rwlock->waiting_readers || rwlock->waiting_writers) {
        while(!mpsc_fifo_peek(&rwlock->waiters, &data)) {};//get info about the next waiter to be woken

        fiber_t* const the_fiber = (fiber_t*)data;
        if(the_fiber->scratch == FIBER_RWLOCK_SCRATCH_WRITE) {
            //next waiter gets write access; only wake him if he can take write access
            if(rwlock->counter == 0) {
                rwlock->waiting_writers -= 1;
                assert(rwlock->waiting_writers >= 0);
                rwlock->counter = -1;
                fiber_manager_wake_from_mpsc_queue(fiber_manager_get(), &rwlock->waiters, 1);
            }
            break;//don't wake anyone else; either the writer got write access or we should not schedule readers until he does
        } else {
            rwlock->waiting_readers -= 1;
            assert(rwlock->waiting_readers >= 0);
            rwlock->counter += 1;
            fiber_manager_wake_from_mpsc_queue(fiber_manager_get(), &rwlock->waiters, 1);
        }
    }
}

int fiber_rwlock_rdunlock(fiber_rwlock_t* rwlock)
{
    assert(rwlock);

    fiber_spinlock_lock(&rwlock->lock);
    assert(rwlock->counter > 0);//make sure we had the lock in the first place
    rwlock->counter -= 1;
    fiber_rwlock_wake_waiters(rwlock);
    fiber_spinlock_unlock(&rwlock->lock);
    return FIBER_SUCCESS;
}

int fiber_rwlock_wrunlock(fiber_rwlock_t* rwlock)
{
    assert(rwlock);

    fiber_spinlock_lock(&rwlock->lock);
    assert(rwlock->counter == -1);//make sure we had the lock in the first place
    rwlock->counter = 0;
    fiber_rwlock_wake_waiters(rwlock);
    fiber_spinlock_unlock(&rwlock->lock);
    return FIBER_SUCCESS;
}

