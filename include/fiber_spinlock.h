#ifndef _FIBER_SPINLOCK_H_
#define _FIBER_SPINLOCK_H_

/*
    Author: Brian Watling
    Email: brianwatling@hotmail.com
    Website: https://github.com/brianwatling

    Description: A spin lock for fibers, based on the ticket lock algorithm
                 found at http://locklessinc.com/articles/locks/. This is meant
                 to be used in places where a fiber does not want to or cannot
                 perform a context switch.
*/

#include <stddef.h>
#include <stdint.h>

typedef union
{
    struct {
        uint32_t ticket;
        uint32_t users;
    } __attribute__ ((packed)) counters;
    uint64_t blob;
} __attribute__ ((packed)) fiber_spinlock_internal_t;

typedef struct fiber_spinlock
{
    fiber_spinlock_internal_t state;
} fiber_spinlock_t;

#ifdef __cplusplus
extern "C" {
#endif

#define FIBER_SPINLOCK_INITIALIER {}

extern int fiber_spinlock_init(fiber_spinlock_t* spinlock);

extern int fiber_spinlock_destroy(fiber_spinlock_t* spinlock);

extern int fiber_spinlock_lock(fiber_spinlock_t* spinlock);

extern int fiber_spinlock_trylock(fiber_spinlock_t* spinlock);

extern int fiber_spinlock_unlock(fiber_spinlock_t* spinlock);

#ifdef __cplusplus
}
#endif

#endif
