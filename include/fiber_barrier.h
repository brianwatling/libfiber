#ifndef _FIBER_BARRIER_H_
#define _FIBER_BARRIER_H_

/*
    Author: Brian Watling
    Email: brianwatling@hotmail.com
    Website: https://github.com/brianwatling
*/

#include "mpsc_fifo.h"

typedef struct fiber_barrier {
    uint32_t count;
    volatile uint64_t counter;
    mpsc_fifo_t waiters;
} fiber_barrier_t;

#define FIBER_BARRIER_SERIAL_FIBER (1)

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
