#ifndef _FIBER_MANAGER_H_
#define _FIBER_MANAGER_H_

#include "fiber.h"
#include "fiber_mutex.h"
#include "fiber_spinlock.h"
#include "work_stealing_deque.h"
#include "mpsc_fifo.h"

typedef struct fiber_mpsc_to_push
{
    mpsc_fifo_t* fifo;
    mpsc_node_t* node;
} fiber_mpsc_to_push_t;

typedef struct fiber_manager
{
    fiber_t* maintenance_fiber;
    fiber_t* volatile current_fiber;
    fiber_t* thread_fiber;
    fiber_t* volatile to_schedule;
    fiber_mpsc_to_push_t mpsc_to_push;
    fiber_mutex_t* volatile mutex_to_unlock;
    fiber_spinlock_t* volatile spinlock_to_unlock;
    void** volatile set_wait_location;
    void* volatile set_wait_value;
    wsd_work_stealing_deque_t* queue_one;
    wsd_work_stealing_deque_t* queue_two;
    wsd_work_stealing_deque_t* volatile schedule_from;
    wsd_work_stealing_deque_t* volatile store_to;
    wsd_work_stealing_deque_t* done_fibers;
    fiber_t* volatile done_fiber;
    /* TODO: done_fibers may be better as a global queue to increase re-use, with the cost of added contention */
    /* TODO: done_fibers could also be setup to allow a thread to steal done fibers from other threads */
    int id;
    size_t yield_count;
} fiber_manager_t;

#ifdef __cplusplus
extern "C" {
#endif

extern fiber_manager_t* fiber_manager_create();

extern void fiber_manager_schedule(fiber_manager_t* manager, fiber_t* the_fiber);

extern void fiber_manager_yield(fiber_manager_t* manager);

extern fiber_manager_t* fiber_manager_get();

/* this should be called immediately when the applicaion starts */
extern int fiber_manager_init(size_t num_threads);

extern void fiber_shutdown();

#define FIBER_MANAGER_STATE_NONE (0)
#define FIBER_MANAGER_STATE_STARTED (1)
#define FIBER_MANAGER_STATE_ERROR (2)

extern int fiber_manager_get_state();

extern int fiber_manager_get_kernel_thread_count();

extern void fiber_manager_do_maintenance();

extern void fiber_manager_wait_in_queue(fiber_manager_t* manager, mpsc_fifo_t* fifo);

extern void fiber_manager_wait_in_queue_and_unlock(fiber_manager_t* manager, mpsc_fifo_t* fifo, fiber_mutex_t* mutex);

extern int fiber_manager_wake_from_queue(fiber_manager_t* manager, mpsc_fifo_t* fifo, int count);

extern void fiber_manager_set_and_wait(fiber_manager_t* manager, void** location, void* value);

extern void* fiber_manager_clear_or_wait(fiber_manager_t* manager, void** location);

extern void* fiber_load_symbol(const char* symbol);

extern void fiber_do_real_sleep(uint32_t seconds, uint32_t useconds);

#ifdef __cplusplus
}
#endif

#endif

