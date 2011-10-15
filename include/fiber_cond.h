#ifndef _FIBER_COND_H_
#define _FIBER_COND_H_

#include "fiber_mutex.h"
#include "mpsc_fifo.h"

/*
    Author: Brian Watling
    Email: brianwatling@hotmail.com
    Website: https://github.com/brianwatling

    Description: A condition variable structure for fibers.
*/

typedef struct fiber_cond
{
    fiber_mutex_t* caller_mutex;
    volatile int waiter_count;
    mpsc_fifo_t waiters;
    fiber_mutex_t internal_mutex;
} fiber_cond_t;

#ifdef __cplusplus
extern "C" {
#endif

extern int fiber_cond_init(fiber_cond_t* cond);

extern void fiber_cond_destroy(fiber_cond_t* cond);

extern int fiber_cond_signal(fiber_cond_t* cond);

extern int fiber_cond_broadcast(fiber_cond_t* cond);

extern int fiber_cond_timedwait(fiber_cond_t* cond, fiber_mutex_t* mutex, const struct timespec* abstime);

extern int fiber_cond_wait(fiber_cond_t* cond, fiber_mutex_t * mutex);

#ifdef __cplusplus
}
#endif

#endif

