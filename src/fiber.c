#include "fiber.h"
#include "fiber_manager.h"
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

static void fiber_go_function(void* param)
{
    fiber_t* the_fiber = (fiber_t*)param;

    the_fiber->run_function(the_fiber->param);

    the_fiber->state = FIBER_STATE_DONE;

    fiber_manager_t* const manager = fiber_manager_get();
    wsd_work_stealing_deque_push_bottom(manager->done_fibers, the_fiber);
    fiber_manager_yield(manager);
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

    ret->state = FIBER_STATE_RUNNING;
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

