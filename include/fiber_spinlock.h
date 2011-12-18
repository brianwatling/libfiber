#ifndef _FIBER_SPINLOCK_H_
#define _FIBER_SPINLOCK_H_

/*
    Author: Brian Watling
    Email: brianwatling@hotmail.com
    Website: https://github.com/brianwatling

    Description: A spin lock for fibers. This is meant to be used in places
                 where a fiber does not want to or cannot perform a context
                 switch. A fiber will spin on this lock without yielding to
                 other fibers, but will yield to other system threads after
                 spinning for a while.
*/

#include <stddef.h>
#include <stdint.h>

typedef struct fiber_spinlock
{
    volatile uint8_t state;
} fiber_spinlock_t;

#ifdef __cplusplus
extern "C" {
#endif

#define FIBER_SPINLOCK_INITIALIER {0}

extern int fiber_spinlock_init(fiber_spinlock_t* spinlock);

extern int fiber_spinlock_destroy(fiber_spinlock_t* spinlock);

extern int fiber_spinlock_lock(fiber_spinlock_t* spinlock);

extern int fiber_spinlock_trylock(fiber_spinlock_t* spinlock, size_t tries);

extern int fiber_spinlock_unlock(fiber_spinlock_t* spinlock);

#ifdef __cplusplus
}
#endif

#endif
