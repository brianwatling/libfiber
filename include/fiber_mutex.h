#ifndef _FIBER_MUTEX_H_
#define _FIBER_MUTEX_H_

#include "fiber.h"
#include "mpsc_fifo.h"

/*
    Author: Brian Watling
    Email: brianwatling@hotmail.com
    Website: https://github.com/brianwatling

    Description: A wait-free fast-path mutex. A mutex maintains an internal counter.
                 The counter is initially one and is atomically decremented by each fiber
                 attempting to acquire the lock. The fiber that decrements the counter
                 from 1 to 0 owns the lock. Fibers which decrement the counter
                 to a value below 0 must wait. Unlocking is done by atomically incrementing
                 the counter. The unlocker must wake up a waiter if the counter is not 1
                 after an unlock operation (ie. other fibers were waiting). Locking the
                 mutex is wait-free - that is, there's no while(!CAS(...)){...} loop;
                 you either get the lock or immediately enter the queue. Unlocking can
                 block if there's contention on the mutex - the unlocker will yield
                 until there's a waiter in the queue (race condition between decrementing
                 the counter and entering the queue). Unlocking is wait-free if there's no
                 contention or if there's already a waiter in the queue.

                 NOTE: I assume atomic increment/decrement is wait-free. If this
                 is implemented using a CAS loop, then replace wait-free with lock-free
                 in this documentation.

    Properties: 1. Wait-free lock operation (but obviously if the lock is not acquired the fiber yields)
                2. Unlock operation can block under contention (generally should be wait-free under high contention)
                3. Wait-free unlock operation without contention
*/

typedef struct fiber_mutex
{
    volatile int counter;
    mpsc_fifo_t* waiters;
} fiber_mutex_t;

#ifdef __cplusplus
extern "C" {
#endif

extern int fiber_mutex_init(fiber_mutex_t* mutex);

extern int fiber_mutex_destroy(fiber_mutex_t* mutex);

extern int fiber_mutex_lock(fiber_mutex_t* mutex);

extern int fiber_mutex_trylock(fiber_mutex_t* mutex);

extern int fiber_mutex_unlock(fiber_mutex_t* mutex);

#ifdef __cplusplus
}
#endif

#endif

