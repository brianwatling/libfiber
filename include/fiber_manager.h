#ifndef _FIBER_MANAGER_H_
#define _FIBER_MANAGER_H_

#include "fiber.h"
#include "work_stealing_deque.h"
#include "mpsc_fifo.h"
#include "mpmc_queue.h"

typedef struct fiber_mpsc_to_push
{
    mpsc_fifo_t* fifo;
    size_t id;
    spsc_node_t* node;
} fiber_mpsc_to_push_t;

typedef struct fiber_spsc_to_push
{
    spsc_fifo_t* fifo;
    spsc_node_t* node;
} fiber_spsc_to_push_t;

typedef struct fiber_mpmc_to_push
{
    mpmc_queue_t* queue;
    mpmc_queue_node_t* node;
} fiber_mpmc_to_push_t;

typedef struct fiber_manager
{
    fiber_t* volatile current_fiber;
    fiber_t* thread_fiber;
    fiber_t* volatile to_schedule;
    fiber_mpsc_to_push_t mpsc_to_push;
    fiber_spsc_to_push_t spsc_to_push;
    fiber_mpmc_to_push_t mpmc_to_push;
    wsd_work_stealing_deque_t* queue_one;
    wsd_work_stealing_deque_t* queue_two;
    wsd_work_stealing_deque_t* volatile schedule_from;
    wsd_work_stealing_deque_t* volatile store_to;
    wsd_work_stealing_deque_t* done_fibers;
    /* TODO: done_fibers may be better as a global queue to increase re-use, with the cost of added contention */
    /* TODO: done_fibers could also be setup to allow a thread to steal done fibers from other threads */
    int id;
    size_t yield_count;
} fiber_manager_t;

#ifdef __cplusplus
extern "C" {
#endif

extern fiber_manager_t* fiber_manager_create();

extern void fiber_manager_destroy(fiber_manager_t* manager);

extern void fiber_manager_schedule(fiber_manager_t* manager, fiber_t* the_fiber);

extern void fiber_manager_yield(fiber_manager_t* manager);

extern fiber_manager_t* fiber_manager_get();

/* this should be called immediately when the applicaion starts */
extern int fiber_manager_set_total_kernel_threads(size_t num_threads);

#define FIBER_MANAGER_STATE_NONE (0)
#define FIBER_MANAGER_STATE_STARTED (1)
#define FIBER_MANAGER_STATE_ERROR (2)

extern int fiber_manager_get_state();

extern int fiber_manager_get_kernel_thread_count();

extern void fiber_manager_do_maintenance();

extern void fiber_manager_wait_in_queue(fiber_manager_t* manager, mpsc_fifo_t* fifo);

extern void fiber_manager_wake_from_queue(fiber_manager_t* manager, mpsc_fifo_t* fifo, int count);

extern void fiber_manager_wait_in_spsc_queue(fiber_manager_t* manager, spsc_fifo_t* fifo);

extern void fiber_manager_wake_from_spsc_queue(fiber_manager_t* manager, spsc_fifo_t* fifo, int count);

extern void fiber_manager_wait_in_mpmc_queue(fiber_manager_t* manager, mpmc_queue_t* queue);

extern void fiber_manager_wake_from_mpmc_queue(fiber_manager_t* manager, mpmc_queue_t* queue, int count);

#ifdef __cplusplus
}
#endif

#endif

