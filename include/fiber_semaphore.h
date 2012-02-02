#ifndef _FIBER_SEMAPHORE_H_
#define _FIBER_SEMAPHORE_H_

/*
    Author: Brian Watling
    Email: brianwatling@hotmail.com
    Website: https://github.com/brianwatling

    Description: A mutex for fibers. A mutex maintains an internal counter.
                 The counter is initially one and is atomically decremented by each fiber
                 attempting to acquire the lock. The fiber that decrements the counter
                 from 1 to 0 owns the lock. Fibers which decrement the counter
                 to a value below 0 must wait. Unlocking is done by atomically incrementing
                 the counter. The unlocker must wake up a waiter if the counter is not 1
                 after an unlock operation (ie. other fibers were waiting).
*/

#include "mpsc_fifo.h"

typedef struct fiber_semaphore
{
    volatile int counter;
    mpsc_fifo_t waiters;
} fiber_semaphore_t;

#ifdef __cplusplus
extern "C" {
#endif

extern int fiber_semaphore_init(fiber_semaphore_t* semaphore, int value);

extern int fiber_semaphore_destroy(fiber_semaphore_t* semaphore);

extern int fiber_semaphore_wait(fiber_semaphore_t* semaphore);

extern int fiber_semaphore_trywait(fiber_semaphore_t* semaphore);

extern int fiber_semaphore_post(fiber_semaphore_t* semaphore);

extern int fiber_semaphore_getvalue(fiber_semaphore_t* semaphore);

#ifdef __cplusplus
}
#endif

#endif

