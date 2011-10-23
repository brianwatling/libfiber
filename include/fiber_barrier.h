#ifndef _FIBER_BARRIER_H_
#define _FIBER_BARRIER_H_

/*
    Author: Brian Watling
    Email: brianwatling@hotmail.com
    Website: https://github.com/brianwatling
*/

typedef struct fiber_barrier {
    int count;
    int remaining;
    mpsc_fifo_t waiters;
} fiber_barrier_t;

#ifdef __cplusplus
extern "C" {
#endif

extern int fiber_barrier_init(fiber_barrier_t* barrier, int count);

extern void fiber_barrier_destroy(fiber_barrier_t* barrier);

extern int fiber_barrier_wait(fiber_barrier_t* barrier);

#ifdef __cplusplus
}
#endif

#endif
