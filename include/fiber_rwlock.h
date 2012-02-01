#ifndef _FIBER_RWLOCK_H_
#define _FIBER_RWLOCK_H_

/*
    Author: Brian Watling
    Email: brianwatling@hotmail.com
    Website: https://github.com/brianwatling
*/

#include "mpsc_fifo.h"
#include "fiber_spinlock.h"

typedef struct fiber_rwlock
{
    volatile int counter;
    volatile int waiting_writers;
    volatile int waiting_readers;
    fiber_spinlock_t lock;
    mpsc_fifo_t waiters;
} fiber_rwlock_t;

#ifdef __cplusplus
extern "C" {
#endif

extern int fiber_rwlock_init(fiber_rwlock_t* rwlock);

extern void fiber_rwlock_destroy(fiber_rwlock_t* rwlock);

extern int fiber_rwlock_rdlock(fiber_rwlock_t* rwlock);

extern int fiber_rwlock_wrlock(fiber_rwlock_t* rwlock);

extern int fiber_rwlock_tryrdlock(fiber_rwlock_t* rwlock);

extern int fiber_rwlock_trywrlock(fiber_rwlock_t* rwlock);

extern int fiber_rwlock_rdunlock(fiber_rwlock_t* rwlock);

extern int fiber_rwlock_wrunlock(fiber_rwlock_t* rwlock);

#ifdef __cplusplus
}
#endif

#endif

