#ifndef _FIBER_MANAGER_H_
#define _FIBER_MANAGER_H_

#include "fiber.h"
#include "work_stealing_deque.h"

typedef struct fiber_manager
{
    fiber_t* volatile current_fiber;
    fiber_t* thread_fiber;
    wsd_work_stealing_deque_t* queue_one;
    wsd_work_stealing_deque_t* queue_two;
    wsd_work_stealing_deque_t* volatile schedule_from;
    wsd_work_stealing_deque_t* volatile store_to;
    wsd_work_stealing_deque_t* done_fibers;
    /* TODO: done_fibers may be better as a global queue to increase re-use, with the cost of added contention */
    /* TODO: done_fibers could also be setup to allow a thread to steal done fibers from other threads */
} fiber_manager_t;

#ifdef __cplusplus
extern "C" {
#endif

extern fiber_manager_t* fiber_manager_create();

extern void fiber_manager_destroy(fiber_manager_t* manager);

extern void fiber_manager_schedule(fiber_manager_t* manager, fiber_t* the_fiber);

extern void fiber_manager_yield(fiber_manager_t* manager);

extern fiber_manager_t* fiber_manager_get();

#ifdef __cplusplus
}
#endif

#endif

