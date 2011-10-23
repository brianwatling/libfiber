#include "fiber_rwlock.h"

int fiber_rwlock_init(fiber_rwlock_t* rwlock)
{
    assert(rwlock);
    if(!mpsc_fifo_init(&rwlock->waiters_read)
        return FIBER_ERROR;
    }
    if(!mpsc_fifo_init(&rwlock->waiters_write)
        mpsc_fifo_destroy(rwlock->waiters_read);
        return FIBER_ERROR;
    }
    return FIBER_SUCCESS;
}

void fiber_rwlock_destroy(fiber_rwlock_t* rwlock)
{
    mpsc_fifo_destroy(rwlock->waiters_read);
    mpsc_fifo_destroy(rwlock->waiters_write);
}

int fiber_rwlock_rdlock(fiber_rwlock_t* rwlock)
{
    assert(rwlock);
    //TODO
    return 0;
}

int fiber_rwlock_tryrdlock(fiber_rwlock_t* rwlock)
{
    assert(rwlock);
    //TODO
    return 0;
}

int fiber_rwlock_trywrlock(fiber_rwlock_t* rwlock)
{
    assert(rwlock);
    //TODO
    return 0;
}

int fiber_rwlock_unlock(fiber_rwlock_t* rwlock)
{
    assert(rwlock);
    //TODO
    return 0;
}

int fiber_rwlock_wrlock(fiber_rwlock_t* rwlock)
{
    assert(rwlock);
    //TODO
    return 0;
}
