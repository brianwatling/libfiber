#include "fiber.h"
#include "fiber_manager.h"
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

static void fiber_go_function(void* param)
{
    fiber_t* the_fiber = (fiber_t*)param;
    fiber_manager_t* manager = fiber_manager_get();

    /* check if anything needs to be scheduled - this is usually done after fiber_swap_context, but we do it here since we aren't going to execute that code. */
    //TODO: re-factor this into "fiber_manager_do_maintenance() or something
    if(manager->to_schedule) {
        wsd_work_stealing_deque_push_bottom(manager->store_to, manager->to_schedule);
        manager->to_schedule = NULL;
    }

    the_fiber->run_function(the_fiber->param);

    the_fiber->state = FIBER_STATE_DONE;

    manager = fiber_manager_get();
    wsd_work_stealing_deque_push_bottom(manager->done_fibers, the_fiber);
    while(1) { /* yield() may actually not switch to anything else if there's nothing else to schedule - loop here until yield() doesn't return */
        fiber_manager_yield(manager);
        usleep(1);/* be a bit nicer */
    }
}

fiber_t* fiber_create(size_t stack_size, fiber_run_function_t run_function, void* param)
{
    fiber_manager_t* const manager = fiber_manager_get();
    fiber_t* ret = wsd_work_stealing_deque_pop_bottom(manager->done_fibers);
    if(ret == WSD_EMPTY || ret == WSD_ABORT) {
        ret = malloc(sizeof(fiber_t));
        if(!ret) {
            errno = ENOMEM;
            return NULL;
        }
        memset(ret, 0, sizeof(*ret));
    } else {
        //we got an old fiber for re-use - destroy the old stack
        fiber_destroy_context(&ret->context);
    }

    ret->state = FIBER_STATE_READY;
    ret->run_function = run_function;
    ret->param = param;
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
    fiber_t* const ret = malloc(sizeof(fiber_t));
    if(!ret) {
        errno = ENOMEM;
        return NULL;
    }
    memset(ret, 0, sizeof(*ret));
    ret->state = FIBER_STATE_RUNNING;
    if(FIBER_SUCCESS != fiber_make_context_from_thread(&ret->context)) {
        free(ret);
        return NULL;
    }
    return ret;
}

#include <stdio.h>

void fiber_join(fiber_t* f)
{
    assert(f);
    /*
        since fibers are never destroyed (they're always re-used), then we can read a
        unique id for the fiber, then yield until it finishes or is reused (ie. the id changes)
    */
    uint64_t const original_id = f->id;
    while(f->state != FIBER_STATE_DONE && f->id == original_id) {
        fiber_manager_yield(fiber_manager_get());/* don't cache the manager - we may switch threads */
    }
}

void fiber_yield()
{
    fiber_manager_yield(fiber_manager_get());
}

