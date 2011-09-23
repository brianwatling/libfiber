#include "fiber_manager.h"
#include <stdlib.h>
#include <errno.h>
#include <string.h>

int fiber_manager_create(fiber_manager_t** dest)
{
    if(!dest) {
        errno = EINVAL;
        return FIBER_ERROR;
    }

    *dest = malloc(sizeof(fiber_manager_t));
    fiber_manager_t* const manager = *dest;
    memset(manager, 0, sizeof(*manager));

    const int create_ret = fiber_create_from_thread(&manager->thread_fiber);
    if(create_ret != FIBER_SUCCESS) {
        return create_ret;
    }

    return FIBER_SUCCESS;
}

void fiber_manager_destroy(fiber_manager_t* manager)
{
    if(manager) {
        fiber_destroy(&manager->thread_fiber);
    }
}

void fiber_manager_schedule(fiber_manager_t* manager, fiber_t* the_fiber)
{
    //TODO
}

void fiber_manager_yield(fiber_manager_t* manager)
{
    //TODO
}

fiber_manager_t* fiber_manager_get()
{
    //TODO
    return NULL;
}

