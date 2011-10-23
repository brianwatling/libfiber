#ifndef _FIBER_MUTEX_H_
#define _FIBER_MUTEX_H_

/*
    Author: Brian Watling
    Email: brianwatling@hotmail.com
    Website: https://github.com/brianwatling
*/

#include "mpsc_fifo.h"

typedef fiber_rwlock
{
    mpsc_fifo_t waiters_read;
    mpsc_fifo_t waiters_write;
} fiber_rwlock_t;

#ifdef __cplusplus
extern "C" {
#endif

extern int fiber_rwlock_init(fiber_rwlock_t* rwlock);

extern void fiber_rwlock_destroy(fiber_rwlock_t* rwlock);

extern int fiber_rwlock_rdlock(fiber_rwlock_t* rwlock);

extern int fiber_rwlock_tryrdlock(fiber_rwlock_t* rwlock);

extern int fiber_rwlock_trywrlock(fiber_rwlock_t* rwlock);

extern int fiber_rwlock_unlock(fiber_rwlock_t* rwlock);

extern int fiber_rwlock_wrlock(fiber_rwlock_t* rwlock);

#ifdef __cplusplus
}
#endif

#endif

