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

#include "fiber.h"
#include "fiber_manager.h"
#include "mpmc_lifo.h"
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

void fiber_join_routine(fiber_t* the_fiber, void* result)
{
    the_fiber->result = result;
    write_barrier();//make sure the result is available before changing the state

    if(the_fiber->detach_state != FIBER_DETACH_DETACHED) {
        const int old_state = atomic_exchange_int((int*)&the_fiber->detach_state, FIBER_DETACH_WAIT_FOR_JOINER);
        if(old_state == FIBER_DETACH_NONE) {
            //need to wait until another fiber joins this one
            fiber_manager_set_and_wait(fiber_manager_get(), (void**)&the_fiber->join_info, the_fiber);
        } else if(old_state == FIBER_DETACH_WAIT_TO_JOIN) {
            //the joining fiber is waiting for us to finish
            fiber_t* const to_schedule = fiber_manager_clear_or_wait(fiber_manager_get(), (void**)&the_fiber->join_info);
            to_schedule->result = the_fiber->result;
            to_schedule->state = FIBER_STATE_READY;
            fiber_manager_schedule(fiber_manager_get(), to_schedule);
        }
    }

    the_fiber->state = FIBER_STATE_DONE;

    fiber_manager_get()->done_fiber = the_fiber;
    fiber_manager_yield(fiber_manager_get());
    assert(0 && "should never get here");
}

#ifdef FIBER_STACK_SPLIT
__attribute__((__no_split_stack__))
#endif
static void* fiber_go_function(void* param)
{
    fiber_t* the_fiber = (fiber_t*)param;

    /* do maintenance - this is usually done after fiber_context_swap, but we do it here too since we are coming from a new place */
    fiber_manager_do_maintenance();

    void* const result = the_fiber->run_function(the_fiber->param);

    fiber_join_routine(the_fiber, result);

    return NULL;
}

mpmc_lifo_t fiber_free_fibers = MPMC_LIFO_INITIALIZER;

fiber_t* fiber_create_no_sched(size_t stack_size, fiber_run_function_t run_function, void* param)
{
    mpsc_fifo_node_t* const node = mpmc_lifo_pop(&fiber_free_fibers);
    fiber_t* ret = NULL;
    if(!node) {
        ret = calloc(1, sizeof(*ret));
        if(!ret) {
            errno = ENOMEM;
            return NULL;
        }
        ret->mpsc_fifo_node = calloc(1, sizeof(*ret->mpsc_fifo_node));
        if(!ret->mpsc_fifo_node) {
            free(ret);
            errno = ENOMEM;
            return NULL;
        }
    } else {
        ret = (fiber_t*)node->data;
        ret->mpsc_fifo_node = node;
        //we got an old fiber for re-use - destroy the old stack
        fiber_context_destroy(&ret->context);
    }

    assert(ret->mpsc_fifo_node);

    ret->run_function = run_function;
    ret->param = param;
    ret->state = FIBER_STATE_READY;
    ret->detach_state = FIBER_DETACH_NONE;
    ret->join_info = NULL;
    ret->result = NULL;
    ret->id += 1;
    if(FIBER_SUCCESS != fiber_context_init(&ret->context, stack_size, &fiber_go_function, ret)) {
        free(ret);
        return NULL;
    }

    return ret;
}

fiber_t* fiber_create(size_t stack_size, fiber_run_function_t run_function, void* param)
{
    fiber_t* const ret = fiber_create_no_sched(stack_size, run_function, param);
    if(ret) {
        fiber_manager_schedule(fiber_manager_get(), ret);
    }
    return ret;
}

fiber_t* fiber_create_from_thread()
{
    fiber_t* const ret = calloc(1, sizeof(*ret));
    if(!ret) {
        errno = ENOMEM;
        return NULL;
    }
    ret->mpsc_fifo_node = calloc(1, sizeof(*ret->mpsc_fifo_node));
    if(!ret->mpsc_fifo_node) {
        free(ret);
        errno = ENOMEM;
        return NULL;
    }

    ret->state = FIBER_STATE_RUNNING;
    ret->detach_state = FIBER_DETACH_NONE;
    ret->join_info = NULL;
    ret->result = NULL;
    ret->id = 1;
    if(FIBER_SUCCESS != fiber_context_init_from_thread(&ret->context)) {
        free(ret);
        return NULL;
    }
    return ret;
}

#include <stdio.h>

int fiber_join(fiber_t* f, void** result)
{
    assert(f);
    if(result) {
        *result = NULL;
    }
    if(f->detach_state == FIBER_DETACH_DETACHED) {
        return FIBER_ERROR;
    }

    const int old_state = atomic_exchange_int((int*)&f->detach_state, FIBER_DETACH_WAIT_TO_JOIN);
    if(old_state == FIBER_DETACH_NONE) {
        //need to wait till the fiber finishes
        fiber_manager_t* const manager = fiber_manager_get();
        fiber_t* const current_fiber = manager->current_fiber;
        fiber_manager_set_and_wait(manager, (void**)&f->join_info, current_fiber);
        if(result) { 
            *result = current_fiber->result;
        }
        current_fiber->result = NULL;
    } else if(old_state == FIBER_DETACH_WAIT_FOR_JOINER) {
        //the other fiber is waiting for us to join
        if(result) { 
            *result = f->result;
        }
        fiber_t* const to_schedule = fiber_manager_clear_or_wait(fiber_manager_get(), (void**)&f->join_info);
        to_schedule->state = FIBER_STATE_READY;
        fiber_manager_schedule(fiber_manager_get(), to_schedule);
    } else {
        //it's either WAIT_TO_JOIN or DETACHED - that's an error!
        return FIBER_ERROR;
    }

    return FIBER_SUCCESS;
}

int fiber_tryjoin(fiber_t* f, void** result)
{
    assert(f);
    if(result) {
        *result = NULL;
    }
    if(f->detach_state == FIBER_DETACH_DETACHED) {
        return FIBER_ERROR;
    }

    if(f->detach_state == FIBER_DETACH_WAIT_FOR_JOINER) {
        //here we've read that the fiber is waiting to be joined.
        //if the fiber is still waiting to be joined after we atmically change its state,
        //then we can go ahead and wake it up. if the fiber's state has changed, we can
        //assume the fiber has been detached or has be joined by some other fiber
        const int old_state = atomic_exchange_int((int*)&f->detach_state, FIBER_DETACH_WAIT_TO_JOIN);
        if(old_state == FIBER_DETACH_WAIT_FOR_JOINER) {
            //the other fiber is waiting for us to join
            if(result) { 
                *result = f->result;
            }
            fiber_t* const to_schedule = fiber_manager_clear_or_wait(fiber_manager_get(), (void**)&f->join_info);
            to_schedule->state = FIBER_STATE_READY;
            fiber_manager_schedule(fiber_manager_get(), to_schedule);
            return FIBER_SUCCESS;
        }
    }

    return FIBER_ERROR;
}

int fiber_yield()
{
    fiber_manager_yield(fiber_manager_get());
    return 1;
}

int fiber_detach(fiber_t* f)
{
    if(!f) {
        return FIBER_ERROR;
    }
    const int old_state = atomic_exchange_int((int*)&f->detach_state, FIBER_DETACH_DETACHED);
    if(old_state == FIBER_DETACH_WAIT_FOR_JOINER
       || old_state == FIBER_DETACH_WAIT_TO_JOIN) {
        //wake up the fiber or the fiber trying to join it (this second case is a convenience, pthreads specifies undefined behaviour in that case)
        fiber_t* const to_schedule = fiber_manager_clear_or_wait(fiber_manager_get(), (void**)&f->join_info);
        to_schedule->state = FIBER_STATE_READY;
        fiber_manager_schedule(fiber_manager_get(), to_schedule);
    } else if(old_state == FIBER_DETACH_DETACHED) {
        return FIBER_ERROR;
    }
    return FIBER_SUCCESS;
}

