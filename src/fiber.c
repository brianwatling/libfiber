#include "fiber.h"
#include "fiber_manager.h"
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

static void* fiber_go_function(void* param)
{
    fiber_t* the_fiber = (fiber_t*)param;

    /* do maintenance - this is usually done after fiber_swap_context, but we do it here too since we are coming from a new place */
    fiber_manager_do_maintenance();

    the_fiber->result = the_fiber->run_function(the_fiber->param);
    write_barrier();
    the_fiber->state = FIBER_STATE_DONE;
    write_barrier();

    while(!the_fiber->detached) {
        fiber_manager_yield(fiber_manager_get());
        //usleep(1);/* be a bit nicer */
        //TODO: not busy loop here.
    }

    wsd_work_stealing_deque_push_bottom(fiber_manager_get()->done_fibers, the_fiber);
    while(1) { /* yield() may actually not switch to anything else if there's nothing else to schedule - loop here until yield() doesn't return */
        fiber_manager_yield(fiber_manager_get());
        //usleep(1);/* be a bit nicer */
    }
    return NULL;
}

fiber_t* fiber_create(size_t stack_size, fiber_run_function_t run_function, void* param)
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
    ret->detached = 0;
    ret->result = NULL;
    ret->id += 1;
    if(FIBER_SUCCESS != fiber_make_context(&ret->context, stack_size, &fiber_go_function, ret)) {
        free(ret);
        return NULL;
    }

    fiber_manager_schedule(manager, ret);

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
    ret->detached = 0;
    ret->result = NULL;
    ret->id = 1;
    if(FIBER_SUCCESS != fiber_make_context_from_thread(&ret->context)) {
        free(ret);
        return NULL;
    }
    return ret;
}

#include <stdio.h>

int fiber_join(fiber_t* f)
{
    assert(f);
    if(f->detached) {
        return FIBER_ERROR;
    }
    /*
        since fibers are never destroyed (they're always re-used), then we can read a
        unique id for the fiber, then yield until it finishes or is reused (ie. the id changes)
    */
    uint64_t const original_id = f->id;
    while(f->state != FIBER_STATE_DONE && f->id == original_id) {
        fiber_manager_yield(fiber_manager_get());/* don't cache the manager - we may switch threads */
    }
    f->detached = 1;
    write_barrier();
    return FIBER_SUCCESS;
}

int fiber_yield()
{
    fiber_manager_yield(fiber_manager_get());
    return 1;
}

int fiber_detach(fiber_t* f)
{
    if(!f || f->detached) {
        return FIBER_ERROR;
    }
    f->detached = 1;
    write_barrier();
    return FIBER_SUCCESS;
}

