/*
 * Copyright (c) 2012-2015, Brian Watling and other contributors
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

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
        //the raiser will not wake this fiber until scratch has been set to FIBER_SIGNAL_READY_TO_WAKE, which the fiber manager will set after the context switch
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
            manager->signal_spin_count += 1;
        }
        old->state = FIBER_STATE_READY;
        fiber_manager_schedule(manager, old);
        return 1;
    }
    return 0;
}

typedef union fiber_multi_signal
{
    struct {
        uintptr_t volatile counter;
        mpsc_fifo_node_t* volatile head;
    } data;
    pointer_pair_t blob;
} __attribute__ ((__packed__)) __attribute__((__aligned__(2 * sizeof(void *)))) fiber_multi_signal_t;

#define FIBER_MULTI_SIGNAL_RAISED ((mpsc_fifo_node_t*)(intptr_t)-1)

static inline void fiber_multi_signal_init(fiber_multi_signal_t* s)
{
    assert(sizeof(*s) == 2 * sizeof(void*));
    s->data.counter = 0;
    s->data.head = NULL;
}

static inline void fiber_multi_signal_destroy(fiber_multi_signal_t* s)
{
    assert(!s || !s->data.head || s->data.head == FIBER_MULTI_SIGNAL_RAISED);
}

static inline void fiber_multi_signal_wait(fiber_multi_signal_t* s)
{
    assert(s);

    fiber_manager_t* const manager = fiber_manager_get();
    fiber_t* const this_fiber = manager->current_fiber;
    this_fiber->scratch = NULL;//clear scratch before marking this fiber to be signalled
    mpsc_fifo_node_t* const node = this_fiber->mpsc_fifo_node;
    assert(node);
    node->data = this_fiber;

    fiber_multi_signal_t snapshot;
    while(1) {
        snapshot.data.counter = s->data.counter;
        load_load_barrier();//read the counter first - this ensures nothing changes while we're working
        snapshot.data.head = s->data.head;

        if(snapshot.data.head == FIBER_MULTI_SIGNAL_RAISED) {
            //try to switch from raised to no waiter -> on success we wake up since we accepted the signal
            fiber_multi_signal_t new_value;
            new_value.data.counter = snapshot.data.counter + 1;
            new_value.data.head = NULL;
            if(compare_and_swap2(&s->blob, &snapshot.blob, &new_value.blob)) {
                break;
            }
        } else {
            //0 or more waiters.
            //try to push self into the waiter list -> on success we sleep
            node->next = snapshot.data.head;
            fiber_multi_signal_t new_value;
            new_value.data.counter = snapshot.data.counter + 1;
            new_value.data.head = node;
            if(compare_and_swap2(&s->blob, &snapshot.blob, &new_value.blob)) {
                assert(this_fiber->state == FIBER_STATE_RUNNING);
                this_fiber->state = FIBER_STATE_WAITING;
                //the raiser will not wake this fiber until scratch has been set to FIBER_SIGNAL_READY_TO_WAKE, which the fiber manager will set after the context switch
                manager->set_wait_location = (void**)&this_fiber->scratch;
                manager->set_wait_value = FIBER_SIGNAL_READY_TO_WAKE;
                fiber_manager_yield(manager);
                this_fiber->scratch = NULL;
                break;
            }
        }
        cpu_relax();
    }
}

//potentially wakes a fiber. if the signal is already raised, the signal will be left in the raised state without waking a fiber. returns 1 if a fiber was woken
static inline int fiber_multi_signal_raise(fiber_multi_signal_t* s)
{
    assert(s);

    fiber_multi_signal_t snapshot;
    while(1) {
        snapshot.data.counter = s->data.counter;
        load_load_barrier();//read the counter first - this ensures nothing changes while we're working
        snapshot.data.head = s->data.head;

        if(!snapshot.data.head || snapshot.data.head == FIBER_MULTI_SIGNAL_RAISED) {
            //raise the signal. changing from 'raised' to 'raised' is required to ensure no wake ups are missed
            fiber_multi_signal_t new_value;
            new_value.data.counter = snapshot.data.counter + 1;
            new_value.data.head = FIBER_MULTI_SIGNAL_RAISED;
            if(compare_and_swap2(&s->blob, &snapshot.blob, &new_value.blob)) {
                break;
            }
        } else {
            //there's a waiter -> try to wake him
            fiber_multi_signal_t new_value;
            new_value.data.counter = snapshot.data.counter + 1;
            new_value.data.head = snapshot.data.head->next;
            if(compare_and_swap2(&s->blob, &snapshot.blob, &new_value.blob)) {
                //we successfully signalled a waiting fiber
                fiber_t* to_wake = (fiber_t*)snapshot.data.head->data;
                to_wake->mpsc_fifo_node = snapshot.data.head;
                fiber_manager_t* const manager = fiber_manager_get();
                while(to_wake->scratch != FIBER_SIGNAL_READY_TO_WAKE) {
                    cpu_relax();//the other fiber is still in the process of going to sleep
                    manager->multi_signal_spin_count += 1;
                }
                to_wake->state = FIBER_STATE_READY;
                fiber_manager_schedule(manager, to_wake);
                return 1;
            }
        }
        cpu_relax();
    }
    return 0;
}

//wakes exactly one fiber
static inline void fiber_multi_signal_raise_strict(fiber_multi_signal_t* s)
{
    assert(s);

    fiber_multi_signal_t snapshot;
    while(1) {
        snapshot.data.counter = s->data.counter;
        load_load_barrier();//read the counter first - this ensures nothing changes while we're working
        snapshot.data.head = s->data.head;

        if(snapshot.data.head && snapshot.data.head != FIBER_MULTI_SIGNAL_RAISED) {
            //there's a waiter -> try to wake him
            fiber_multi_signal_t new_value;
            new_value.data.counter = snapshot.data.counter + 1;
            new_value.data.head = snapshot.data.head->next;
            if(compare_and_swap2(&s->blob, &snapshot.blob, &new_value.blob)) {
                //we successfully signalled a waiting fiber
                fiber_t* to_wake = (fiber_t*)snapshot.data.head->data;
                to_wake->mpsc_fifo_node = snapshot.data.head;
                fiber_manager_t* const manager = fiber_manager_get();
                while(to_wake->scratch != FIBER_SIGNAL_READY_TO_WAKE) {
                    cpu_relax();//the other fiber is still in the process of going to sleep
                    manager->multi_signal_spin_count += 1;
                }
                to_wake->state = FIBER_STATE_READY;
                fiber_manager_schedule(manager, to_wake);
                return;
            }
        }
        cpu_relax();
    }
}

#endif

