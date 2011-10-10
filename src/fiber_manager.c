#include "fiber_manager.h"
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>

static int fiber_manager_state = FIBER_MANAGER_STATE_NONE;
static int fiber_manager_num_threads = 0;
static wsd_work_stealing_deque_t** fiber_mananger_thread_queues = NULL;
static pthread_t* fiber_manager_threads = NULL;
static fiber_manager_t** fiber_managers = NULL;

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

static void fiber_load_balance(fiber_manager_t* manager)
{
    size_t i = 2 * (manager->id + 1);
    const size_t end = i + 2 * (fiber_manager_num_threads - 1);
    const size_t mod = 2 * fiber_manager_num_threads;
    size_t local_count = wsd_work_stealing_deque_size(manager->schedule_from);
    for(; i < end; ++i) {
        const size_t index = i % mod;
        wsd_work_stealing_deque_t* const remote_queue = fiber_mananger_thread_queues[index];
        assert(remote_queue != manager->queue_one);
        assert(remote_queue != manager->queue_two);
        if(!remote_queue) {
            continue;
        }
        size_t remote_count = wsd_work_stealing_deque_size(remote_queue);
        while(remote_count > local_count) {
            fiber_t* const stolen = (fiber_t*)wsd_work_stealing_deque_steal(remote_queue);
            if(stolen == WSD_EMPTY || stolen == WSD_ABORT) {
                break;
            }
            assert(stolen->state == FIBER_STATE_READY);
            wsd_work_stealing_deque_push_bottom(manager->schedule_from, stolen);
            --remote_count;
            ++local_count;
        }
    }
}

//static int fiber_manager_load_balance_guard = 0;

void fiber_manager_yield(fiber_manager_t* manager)
{
    assert(fiber_manager_state == FIBER_MANAGER_STATE_STARTED);
    assert(manager);
    if(wsd_work_stealing_deque_size(manager->schedule_from) == 0) {
        wsd_work_stealing_deque_t* const temp = manager->schedule_from;
        manager->schedule_from = manager->store_to;
        manager->store_to = temp;
    }

    manager->yield_count += 1;
    //occasionally steal some work from threads with more load
    //TODO: evaluate whether guarding this is worthwhile
    if((manager->yield_count & 1023) == 0) {// && __sync_bool_compare_and_swap(&fiber_manager_load_balance_guard, 0, 1)) {
        fiber_load_balance(manager);
        //fiber_manager_load_balance_guard = 0;
    }

    if(wsd_work_stealing_deque_size(manager->schedule_from) > 0) {
        fiber_t* const new_fiber = (fiber_t*)wsd_work_stealing_deque_pop_bottom(manager->schedule_from);
        if(new_fiber != WSD_EMPTY && new_fiber != WSD_ABORT) {
            fiber_t* const old_fiber = manager->current_fiber;
            if(old_fiber->state == FIBER_STATE_RUNNING) {
                old_fiber->state = FIBER_STATE_READY;
                manager->to_schedule = old_fiber;/* must schedule it *after* fiber_swap_context, else another thread can start executing an invalid context */
            }
            manager->current_fiber = new_fiber;
            new_fiber->state = FIBER_STATE_RUNNING;
            fiber_swap_context(&old_fiber->context, &new_fiber->context);

            fiber_manager_do_maintenance();
        }
    }
}

#ifdef USE_COMPILER_THREAD_LOCAL
static __thread fiber_manager_t* fiber_the_manager = NULL;
#else
static pthread_key_t fiber_manager_key;
static pthread_once_t fiber_manager_key_once = PTHREAD_ONCE_INIT;

static void fiber_manager_make_key()
{
    const int ret = pthread_key_create(&fiber_manager_key, NULL);
    if(ret) {
        assert(0 && "pthread_key_create() failed!");
        abort();
    }
}
#endif

fiber_manager_t* fiber_manager_get()
{
#ifdef USE_COMPILER_THREAD_LOCAL
    if(!fiber_the_manager) {
        fiber_the_manager = fiber_manager_create();
        assert(fiber_the_manager);
    }
    return fiber_the_manager;
#else
    (void)pthread_once(&fiber_manager_key_once, &fiber_manager_make_key);
    fiber_manager_t* ret = (fiber_manager_t*)pthread_getspecific(fiber_manager_key);
    if(!ret) {
        ret = fiber_manager_create();
        assert(ret);
        if(pthread_setspecific(fiber_manager_key, ret)) {
            assert(0 && "pthread_setspecific() failed!");
            abort();
        }
    }
    return ret;
#endif
}

static void* fiber_manager_thread_func(void* param)
{
    /* set the thread local, then start running fibers */
#ifdef USE_COMPILER_THREAD_LOCAL
    fiber_the_manager = (fiber_manager_t*)param;
#else
    const int ret = pthread_setspecific(fiber_manager_key, param);
    if(ret) {
        assert(0 && "pthread_setspecific() failed!");
        abort();
    }
#endif

    while(1) {
        /* always call fiber_manager_get(), because this *thread* fiber will actually switch threads */
        fiber_manager_yield(fiber_manager_get());
    }
    return NULL;
}

int fiber_manager_set_total_kernel_threads(size_t num_threads)
{
    if(fiber_manager_get_state() != FIBER_MANAGER_STATE_NONE) {
        errno = EINVAL;
        return FIBER_ERROR;
    }

    fiber_mananger_thread_queues = malloc(2 * num_threads * sizeof(wsd_work_stealing_deque_t*));
    assert(fiber_mananger_thread_queues);
    memset(fiber_mananger_thread_queues, 0, 2 * num_threads * sizeof(wsd_work_stealing_deque_t*));
    fiber_manager_threads = malloc(num_threads * sizeof(pthread_t));
    assert(fiber_manager_threads);
    memset(fiber_manager_threads, 0, num_threads * sizeof(pthread_t));
    fiber_manager_num_threads = num_threads;
    fiber_managers = malloc(num_threads * sizeof(fiber_manager_t*));
    assert(fiber_managers);
    memset(fiber_managers, 0, num_threads * sizeof(fiber_manager_t*));

    fiber_manager_t* const main_manager = fiber_manager_get();
    fiber_mananger_thread_queues[0] = main_manager->queue_one;
    fiber_mananger_thread_queues[1] = main_manager->queue_two;
    fiber_managers[0] = main_manager;

    fiber_manager_state = FIBER_MANAGER_STATE_STARTED;

    size_t i;
    for(i = 1; i < num_threads; ++i) {
        fiber_manager_t* const new_manager = fiber_manager_create();
        assert(new_manager);
        fiber_mananger_thread_queues[2 * i] = new_manager->queue_one;
        fiber_mananger_thread_queues[2 * i + 1] = new_manager->queue_two;
        new_manager->id = i;
        fiber_managers[i] = new_manager;
    }

    for(i = 1; i < num_threads; ++i) {
        if(pthread_create(&fiber_manager_threads[i], NULL, &fiber_manager_thread_func, fiber_managers[i])) {
            assert(0 && "failed to create kernel thread");
            fiber_manager_state = FIBER_MANAGER_STATE_ERROR;
            abort();
            return FIBER_ERROR;
        }
    }

    return FIBER_SUCCESS;
}

int fiber_manager_get_state()
{
    return fiber_manager_state;
}

int fiber_manager_get_kernel_thread_count()
{
    return fiber_manager_num_threads;
}

void fiber_manager_do_maintenance()
{
    fiber_manager_t* const manager = fiber_manager_get();
    if(manager->to_schedule) {
        wsd_work_stealing_deque_push_bottom(manager->store_to, manager->to_schedule);
        manager->to_schedule = NULL;
    }
}

