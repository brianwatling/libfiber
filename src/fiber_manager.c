#include "fiber_manager.h"
#include "fiber_event.h"
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>
#ifndef __USE_GNU
#define __USE_GNU
#endif
#include <dlfcn.h>

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
    fiber_manager_t* const manager = calloc(1, sizeof(*manager));
    if(!manager) {
        errno = ENOMEM;
        return NULL;
    }

    manager->thread_fiber = fiber_create_from_thread();
    manager->current_fiber = manager->thread_fiber;
    manager->queue_one = wsd_work_stealing_deque_create();
    manager->queue_two = wsd_work_stealing_deque_create();
    manager->schedule_from = manager->queue_one;
    manager->store_to = manager->queue_two;
    manager->done_fibers = wsd_work_stealing_deque_create();

    if(!manager->thread_fiber
       || !manager->queue_one
       || !manager->queue_two
       || !manager->done_fibers) {
        wsd_work_stealing_deque_destroy(manager->queue_one);
        wsd_work_stealing_deque_destroy(manager->queue_two);
        wsd_work_stealing_deque_destroy(manager->done_fibers);
        fiber_destroy(manager->thread_fiber);
        free(manager);
        return NULL;
    }
    return manager;
}

void fiber_manager_schedule(fiber_manager_t* manager, fiber_t* the_fiber)
{
    assert(manager);
    assert(the_fiber);
    assert(the_fiber->state == FIBER_STATE_READY);
    wsd_work_stealing_deque_push_bottom(manager->schedule_from, the_fiber);
}

static int fiber_load_balance(fiber_manager_t* manager)
{
    size_t max_steal = 50;
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
        while(remote_count > local_count && max_steal > 0) {
            fiber_t* const stolen = (fiber_t*)wsd_work_stealing_deque_steal(remote_queue);
            if(stolen == WSD_EMPTY || stolen == WSD_ABORT) {
                break;
            }
            assert(stolen->state == FIBER_STATE_READY);
            wsd_work_stealing_deque_push_bottom(manager->schedule_from, stolen);
            --remote_count;
            ++local_count;
            --max_steal;
        }
    }
    return 1;
}

static void* fiber_manager_thread_func(void* param);

static inline void fiber_manager_switch_to(fiber_manager_t* manager, fiber_t* old_fiber, fiber_t* new_fiber)
{
    if(old_fiber->state == FIBER_STATE_RUNNING) {
        old_fiber->state = FIBER_STATE_READY;
        manager->to_schedule = old_fiber;
    }
    manager->current_fiber = new_fiber;
    new_fiber->state = FIBER_STATE_RUNNING;
    write_barrier();
    fiber_swap_context(&old_fiber->context, &new_fiber->context);

    fiber_manager_do_maintenance();
}

void fiber_manager_yield(fiber_manager_t* manager)
{
    assert(fiber_manager_state == FIBER_MANAGER_STATE_STARTED);
    assert(manager);
    if(wsd_work_stealing_deque_size(manager->schedule_from) == 0) {
        wsd_work_stealing_deque_t* const temp = manager->schedule_from;
        manager->schedule_from = manager->store_to;
        manager->store_to = temp;
    }

    while(1) {
        manager->yield_count += 1;

        if(wsd_work_stealing_deque_size(manager->schedule_from) > 0) {
            fiber_t* const new_fiber = (fiber_t*)wsd_work_stealing_deque_pop_bottom(manager->schedule_from);
            if(new_fiber != WSD_EMPTY && new_fiber != WSD_ABORT) {
                fiber_manager_switch_to(manager, manager->current_fiber, new_fiber);
                break;
            }
        } else if(FIBER_STATE_WAITING == manager->current_fiber->state
                  || FIBER_STATE_DONE == manager->current_fiber->state) {
            if(!manager->maintenance_fiber) {
                manager->maintenance_fiber = fiber_create_no_sched(102400, &fiber_manager_thread_func, manager);
                fiber_detach(manager->maintenance_fiber);
            }

            fiber_manager_switch_to(manager, manager->current_fiber, manager->maintenance_fiber);
            //re-grab the manager, since we could be on a different thread now
            manager = fiber_manager_get();
        } else {
            //occasionally steal some work from threads with more load
            if((manager->yield_count & 1023) == 0) {
                fiber_load_balance(manager);
            }
            break;
        }
    }
}

void* fiber_load_symbol(const char* symbol)
{
    void* ret = dlsym(RTLD_NEXT, symbol);
    if(!ret) {
        ret = dlsym(RTLD_DEFAULT, symbol);
    }
    assert(ret);
    return ret;
}

#ifndef USE_COMPILER_THREAD_LOCAL
typedef int (*pthread_key_create_function)(pthread_key_t*, void (*)(void*));
static pthread_key_create_function pthread_key_create_func = NULL;
typedef void* (*pthread_getspecific_function)(pthread_key_t);
static pthread_getspecific_function pthread_getspecific_func = NULL;
typedef int (*pthread_setspecific_function)(pthread_key_t, const void *);
static pthread_setspecific_function pthread_setspecific_func = NULL;
#endif

#ifdef USE_COMPILER_THREAD_LOCAL
static __thread fiber_manager_t* fiber_the_manager = NULL;
#else
static pthread_key_t fiber_manager_key;
static pthread_once_t fiber_manager_key_once = PTHREAD_ONCE_INIT;

static void fiber_manager_make_key()
{
    if(!pthread_key_create_func) {
        pthread_key_create_func = (pthread_key_create_function)fiber_load_symbol("pthread_key_create");
    }
    const int ret = pthread_key_create_func(&fiber_manager_key, NULL);
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
    if(!pthread_getspecific_func) {
        pthread_getspecific_func = (pthread_getspecific_function)fiber_load_symbol("pthread_getspecific");
    }
    fiber_manager_t* ret = (fiber_manager_t*)pthread_getspecific_func(fiber_manager_key);
    if(!ret) {
        ret = fiber_manager_create();
        assert(ret);
        if(!pthread_setspecific_func) {
            pthread_setspecific_func = (pthread_setspecific_function)fiber_load_symbol("pthread_setspecific");
        }
        if(pthread_setspecific_func(fiber_manager_key, ret)) {
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
    if(!pthread_setspecific_func) {
        pthread_setspecific_func = (pthread_setspecific_function)fiber_load_symbol("pthread_setspecific");
    }
    const int ret = pthread_setspecific_func(fiber_manager_key, param);
    if(ret) {
        assert(0 && "pthread_setspecific() failed!");
        abort();
    }
#endif

    fiber_manager_t* manager = (fiber_manager_t*)param;
    if(!manager->maintenance_fiber) {
        manager->maintenance_fiber = manager->thread_fiber;
    }

    while(1) {
        fiber_load_balance(manager);
        if(wsd_work_stealing_deque_size(manager->schedule_from) == 0) {
            wsd_work_stealing_deque_t* const temp = manager->schedule_from;
            manager->schedule_from = manager->store_to;
            manager->store_to = temp;
            if(wsd_work_stealing_deque_size(manager->schedule_from) == 0) {
                fiber_poll_events();
            }
        }

        if(wsd_work_stealing_deque_size(manager->schedule_from) > 0) {
            fiber_t* const new_fiber = (fiber_t*)wsd_work_stealing_deque_pop_bottom(manager->schedule_from);
            if(new_fiber != WSD_EMPTY && new_fiber != WSD_ABORT) {
                //make this fiber wait, so it won't be pushed into the WSD
                manager->maintenance_fiber->state = FIBER_STATE_WAITING;

                fiber_manager_switch_to(manager, manager->maintenance_fiber, new_fiber);
            }
        }
    }
    return NULL;
}

typedef int (*pthread_create_function)(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*);

int fiber_manager_set_total_kernel_threads(size_t num_threads)
{
    if(fiber_manager_get_state() != FIBER_MANAGER_STATE_NONE) {
        errno = EINVAL;
        return FIBER_ERROR;
    }

    fiber_mananger_thread_queues = calloc(2 * num_threads, sizeof(*fiber_mananger_thread_queues));
    assert(fiber_mananger_thread_queues);
    fiber_manager_threads = calloc(num_threads, sizeof(*fiber_manager_threads));
    assert(fiber_manager_threads);
    fiber_manager_num_threads = num_threads;
    fiber_managers = calloc(num_threads, sizeof(*fiber_managers));
    assert(fiber_managers);

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

    pthread_create_function pthread_create_func = (pthread_create_function)fiber_load_symbol("pthread_create");
    assert(pthread_create_func);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 1024000);

    for(i = 1; i < num_threads; ++i) {
        if(pthread_create_func(&fiber_manager_threads[i], &attr, &fiber_manager_thread_func, fiber_managers[i])) {
            assert(0 && "failed to create kernel thread");
            fiber_manager_state = FIBER_MANAGER_STATE_ERROR;
            abort();
            return FIBER_ERROR;
        }
    }

    pthread_attr_destroy(&attr);

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

extern int fiber_mutex_unlock_internal(fiber_mutex_t* mutex);

void fiber_manager_do_maintenance()
{
    fiber_manager_t* const manager = fiber_manager_get();
    if(manager->to_schedule) {
        assert(manager->to_schedule->state == FIBER_STATE_READY);
        wsd_work_stealing_deque_push_bottom(manager->store_to, manager->to_schedule);
        manager->to_schedule = NULL;
    }

    if(manager->mpsc_to_push.fifo) {
        mpsc_fifo_push(manager->mpsc_to_push.fifo, manager->mpsc_to_push.node);
        memset(&manager->mpsc_to_push, 0, sizeof(manager->mpsc_to_push));
    }

    if(manager->mutex_to_unlock) {
        fiber_mutex_t* const to_unlock = manager->mutex_to_unlock;
        manager->mutex_to_unlock = NULL;
        fiber_mutex_unlock_internal(to_unlock);
    }

    if(manager->spinlock_to_unlock) {
        fiber_spinlock_t* const to_unlock = manager->spinlock_to_unlock;
        manager->spinlock_to_unlock = NULL;
        fiber_spinlock_unlock(to_unlock);
    }

    if(manager->set_wait_location) {
        *manager->set_wait_location = manager->set_wait_value;
        manager->set_wait_location = NULL;
        manager->set_wait_value = NULL;
    }
}

void fiber_manager_wait_in_queue(fiber_manager_t* manager, mpsc_fifo_t* fifo)
{
    assert(manager);
    assert(fifo);
    fiber_t* const this_fiber = manager->current_fiber;
    assert(this_fiber->state == FIBER_STATE_RUNNING);
    assert(this_fiber->mpsc_node);
    this_fiber->state = FIBER_STATE_WAITING;
    manager->mpsc_to_push.fifo = fifo;
    manager->mpsc_to_push.node = this_fiber->mpsc_node;
    manager->mpsc_to_push.node->data = this_fiber;
    this_fiber->mpsc_node = NULL;
    fiber_manager_yield(manager);
}

void fiber_manager_wait_in_queue_and_unlock(fiber_manager_t* manager, mpsc_fifo_t* fifo, fiber_mutex_t* mutex)
{
    manager->mutex_to_unlock = mutex;
    fiber_manager_wait_in_queue(manager, fifo);
}

void fiber_manager_wake_from_queue(fiber_manager_t* manager, mpsc_fifo_t* fifo, int count)
{
    mpsc_node_t* out = NULL;
    int spin_counter = 0;
    do {
        if((out = mpsc_fifo_pop(fifo))) {
            count -= 1;
            fiber_t* const to_schedule = (fiber_t*)out->data;
            assert(to_schedule->state == FIBER_STATE_WAITING);
            assert(!to_schedule->mpsc_node);
            to_schedule->mpsc_node = out;
            to_schedule->state = FIBER_STATE_READY;
            fiber_manager_schedule(manager, to_schedule);
        }
        ++spin_counter;
        if(spin_counter > 100) {
            sched_yield();
            spin_counter = 0;
        }
    } while(count > 0);
}

void fiber_manager_set_and_wait(fiber_manager_t* manager, void** location, void* value)
{
    assert(manager);
    assert(location);
    assert(value);
    fiber_t* const this_fiber = manager->current_fiber;
    assert(this_fiber->state == FIBER_STATE_RUNNING);
    manager->set_wait_location = location;
    manager->set_wait_value = value;
    this_fiber->state = FIBER_STATE_WAITING;
    fiber_manager_yield(manager);
}

void* fiber_manager_clear_or_wait(fiber_manager_t* manager, void** location)
{
    assert(manager);
    assert(location);
    while(1) {
        void* const ret = atomic_exchange_pointer(location, NULL);
        if(ret) {
            return ret;
        }
        fiber_manager_yield(manager);
        manager = fiber_manager_get();
    }
    return NULL;
}

