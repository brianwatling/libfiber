#include "fiber_manager.h"
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

void fiber_destroy(fiber_t* f)
{
    if(f) {
        assert(f->state == FIBER_STATE_DONE);
        fiber_destroy_context(&f->context);
        free(f);
    }
}

fiber_manager_t* fiber_manager_create()
{
    fiber_manager_t* const manager = malloc(sizeof(fiber_manager_t));
    if(!manager) {
        errno = ENOMEM;
        return NULL;
    }

    memset(manager, 0, sizeof(*manager));

    manager->thread_fiber = fiber_create_from_thread();
    if(!manager->thread_fiber) {
        return NULL;
    }

    manager->current_fiber = manager->thread_fiber;
    manager->queue_one = wsd_work_stealing_deque_create();
    if(!manager->queue_one) {
        fiber_destroy(manager->thread_fiber);
        return NULL;
    }
    manager->queue_two = wsd_work_stealing_deque_create();
    if(!manager->queue_two) {
        wsd_work_stealing_deque_destroy(manager->queue_one);
        fiber_destroy(manager->thread_fiber);
        return NULL;
    }
    manager->done_fibers = wsd_work_stealing_deque_create();
    if(!manager->done_fibers) {
        wsd_work_stealing_deque_destroy(manager->queue_two);
        wsd_work_stealing_deque_destroy(manager->queue_one);
        fiber_destroy(manager->thread_fiber);
        return NULL;
        return NULL;
    }

    manager->schedule_from = manager->queue_one;
    manager->store_to = manager->queue_two;

    return manager;
}

void fiber_manager_destroy(fiber_manager_t* manager)
{
    if(manager) {
        fiber_destroy(manager->thread_fiber);
        wsd_work_stealing_deque_destroy(manager->queue_one);
        wsd_work_stealing_deque_destroy(manager->queue_two);
        wsd_work_stealing_deque_destroy(manager->done_fibers);
    }
}

void fiber_manager_schedule(fiber_manager_t* manager, fiber_t* the_fiber)
{
    assert(manager);
    assert(the_fiber);
    wsd_work_stealing_deque_push_bottom(manager->schedule_from, the_fiber);
}

void fiber_manager_yield(fiber_manager_t* manager)
{
    assert(manager);
    if(wsd_work_stealing_deque_size(manager->schedule_from) == 0) {
        wsd_work_stealing_deque_t* const temp = manager->schedule_from;
        manager->schedule_from = manager->store_to;
        manager->store_to = temp;
    }
    if(wsd_work_stealing_deque_size(manager->schedule_from) > 0) {
        fiber_t* const new_fiber = (fiber_t*)wsd_work_stealing_deque_pop_bottom(manager->schedule_from);
        if(new_fiber != WSD_EMPTY && new_fiber != WSD_ABORT) {
            fiber_t* const old_fiber = manager->current_fiber;
            if(old_fiber->state == FIBER_STATE_RUNNING) {
                wsd_work_stealing_deque_push_bottom(manager->store_to, old_fiber);
            }
            manager->current_fiber = new_fiber;
            fiber_swap_context(&old_fiber->context, &new_fiber->context);
        }
    }
}

static fiber_manager_t* fiber_the_manager = NULL;
fiber_manager_t* fiber_manager_get()
{
    if(!fiber_the_manager) {
        fiber_the_manager = fiber_manager_create();
        assert(fiber_the_manager);
    }
    return fiber_the_manager;
}

