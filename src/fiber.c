#include "fiber.h"
#include "fiber_manager.h"
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

void fiber_join_routine(fiber_t* the_fiber, void* result)
{
    the_fiber->result = result;
    write_barrier();

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
            write_barrier();
            fiber_manager_schedule(fiber_manager_get(), to_schedule);
        }
    }

    the_fiber->state = FIBER_STATE_DONE;
    write_barrier();

    wsd_work_stealing_deque_push_bottom(fiber_manager_get()->done_fibers, the_fiber);
    while(1) { /* yield() may actually not switch to anything else if there's nothing else to schedule - loop here until yield() doesn't return */
        fiber_manager_yield(fiber_manager_get());
    }
}

static void* fiber_go_function(void* param)
{
    fiber_t* the_fiber = (fiber_t*)param;

    /* do maintenance - this is usually done after fiber_swap_context, but we do it here too since we are coming from a new place */
    fiber_manager_do_maintenance();

    void* const result = the_fiber->run_function(the_fiber->param);

    fiber_join_routine(the_fiber, result);

    return NULL;
}

fiber_t* fiber_create_no_sched(size_t stack_size, fiber_run_function_t run_function, void* param)
{
    fiber_manager_t* const manager = fiber_manager_get();
    fiber_t* ret = wsd_work_stealing_deque_pop_bottom(manager->done_fibers);
    if(ret == WSD_EMPTY || ret == WSD_ABORT) {
        ret = calloc(1, sizeof(*ret));
        if(!ret) {
            errno = ENOMEM;
            return NULL;
        }
        ret->mpsc_node = calloc(1, sizeof(*ret->mpsc_node));
        if(!ret->mpsc_node) {
            free(ret);
            errno = ENOMEM;
            return NULL;
        }
    } else {
        //we got an old fiber for re-use - destroy the old stack
        fiber_destroy_context(&ret->context);
    }

    assert(ret->mpsc_node);

    ret->run_function = run_function;
    ret->param = param;
    ret->state = FIBER_STATE_READY;
    ret->detach_state = FIBER_DETACH_NONE;
    ret->join_info = NULL;
    ret->result = NULL;
    ret->id += 1;
    if(FIBER_SUCCESS != fiber_make_context(&ret->context, stack_size, &fiber_go_function, ret)) {
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
    ret->mpsc_node = calloc(1, sizeof(*ret->mpsc_node));
    if(!ret->mpsc_node) {
        free(ret);
        errno = ENOMEM;
        return NULL;
    }

    ret->state = FIBER_STATE_RUNNING;
    ret->detach_state = FIBER_DETACH_NONE;
    ret->join_info = NULL;
    ret->result = NULL;
    ret->id = 1;
    if(FIBER_SUCCESS != fiber_make_context_from_thread(&ret->context)) {
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
            load_load_barrier();
            *result = current_fiber->result;
        }
        current_fiber->result = NULL;
    } else if(old_state == FIBER_DETACH_WAIT_FOR_JOINER) {
        //the other fiber is waiting for us to join
        if(result) { 
            *result = f->result;
        }
        load_load_barrier();
        fiber_t* const to_schedule = fiber_manager_clear_or_wait(fiber_manager_get(), (void**)&f->join_info);
        to_schedule->state = FIBER_STATE_READY;
        fiber_manager_schedule(fiber_manager_get(), to_schedule);
    } else {
        //it's either WAIT_TO_JOIN or DETACHED - that's an error!
        return FIBER_ERROR;
    }

    return FIBER_SUCCESS;
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

