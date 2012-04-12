#ifndef _FIBER_SIGNAL_H_
#define _FIBER_SIGNAL_H_

/*
    Author: Brian Watling
    Email: brianwatling@hotmail.com
    Website: https://github.com/brianwatling
*/

#include <assert.h>
#include <stdint.h>

#include "fiber.h"
#include "machine_specific.h"

typedef struct fiber_signal
{
    fiber_t* waiter;
} fiber_signal_t;

#define FIBER_SIGNAL_NO_WAITER ((fiber_t*)0)
#define FIBER_SIGNAL_RAISED ((fiber_t*)(intptr_t)-1)
#define FIBER_SIGNAL_READY_TO_WAKE ((fiber_t*)(intptr_t)-1)

static inline void fiber_signal_init(fiber_signal_t* s)
{
    assert(s);
    s->waiter = FIBER_SIGNAL_NO_WAITER;
}

static inline void fiber_signal_destroy(fiber_signal_t* s)
{
    //empty
}

static inline void fiber_signal_wait(fiber_signal_t* s)
{
    assert(s);

    fiber_manager_t* const manager = fiber_manager_get();
    fiber_t* const this_fiber = manager->current_fiber;
    this_fiber->scratch = NULL;//clear scratch before marking this fiber to be signalled
    if(__sync_bool_compare_and_swap(&s->waiter, FIBER_SIGNAL_NO_WAITER, this_fiber)) {
        //the signal is not raised, we're now waiting
        assert(this_fiber->state == FIBER_STATE_RUNNING);
        this_fiber->state = FIBER_STATE_WAITING;
        //the raiser will not wake this fiber until scratch has been set to FIBER_SIGNAL_RAISED, which the fiber manager will set after the context switch
        manager->set_wait_location = (void**)&this_fiber->scratch;
        manager->set_wait_value = FIBER_SIGNAL_READY_TO_WAKE;
        fiber_manager_yield(manager);
        this_fiber->scratch = NULL;
    }
    //the signal has been raised
    s->waiter = FIBER_SIGNAL_NO_WAITER;
}

//returns 1 if a fiber was woken
static inline int fiber_signal_raise(fiber_signal_t* s)
{
    assert(s);

    fiber_t* const old = (fiber_t*)atomic_exchange_pointer((void**)&s->waiter, FIBER_SIGNAL_RAISED);
    if(old != FIBER_SIGNAL_NO_WAITER && old != FIBER_SIGNAL_RAISED) {
        //we successfully signalled while a fiber was waiting
        s->waiter = FIBER_SIGNAL_NO_WAITER;
        fiber_manager_t* const manager = fiber_manager_get();
        while(old->scratch != FIBER_SIGNAL_READY_TO_WAKE) {
            cpu_relax();//the other fiber is still in the process of going to sleep
            manager->spin_count += 1;
        }
        old->state = FIBER_STATE_READY;
        fiber_manager_schedule(manager, old);
        return 1;
    }
    return 0;
}

#endif

