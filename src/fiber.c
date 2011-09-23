#include "fiber.h"
#include <errno.h>
#include <string.h>
#include <stdlib.h>

static void fiber_go_function(void* param)
{
    fiber_t* the_fiber = (fiber_t*)param;

    the_fiber->run_function(the_fiber->param);

    the_fiber->state = FIBER_STATE_DONE;

    //TODO: fiber_manager_yield(the_fiber->manager);
    //TODO: have self deleted?
}

int fiber_create(fiber_t* dest, size_t stack_size, fiber_run_function_t run_function, void* param)
{
    if(!dest || !run_function || !stack_size) {
        errno = EINVAL;
        return FIBER_ERROR;
    }
    memset(dest, 0, sizeof(*dest));
    dest->state = FIBER_STATE_NONE;
    dest->run_function = run_function;
    dest->param = param;
    return fiber_make_context(&dest->context, stack_size, &fiber_go_function, dest);
}

int fiber_create_from_thread(fiber_t* dest)
{
    if(!dest) {
        errno = EINVAL;
        return FIBER_ERROR;
    }
    memset(dest, 0, sizeof(*dest));
    dest->state = FIBER_STATE_RUNNING;
    return fiber_make_context_from_thread(&dest->context);
}

void fiber_destroy(fiber_t* f)
{
    if(f) {
        f->state = FIBER_STATE_DONE;
        fiber_destroy_context(&f->context);
    }
}

void fiber_join(fiber_t* f)
{
    if(f) {
        //TODO
    }
}

