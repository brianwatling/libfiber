/*
 * Copyright (c) 2012-2015, Brian Watling and other contributors
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "fiber_manager.h"
#include "fiber_event.h"
#include "fiber_io.h"
#include "mpmc_lifo.h"
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
#include "lockfree_ring_buffer.h"

#ifdef FIBER_STACK_SPLIT
void __splitstack_block_signals(int* new, int* old);

void splitstack_disable_block_signals()
{
    int off = 0;
    __splitstack_block_signals(&off, NULL);
}
#else
void splitstack_disable_block_signals()
{
    //nothing - splitstack is not enabled
}
#endif

static int fiber_manager_state = FIBER_MANAGER_STATE_NONE;
static int fiber_manager_num_threads = 0;
static pthread_t* fiber_manager_threads = NULL;
static fiber_manager_t** fiber_managers = NULL;
static volatile int fiber_shutting_down = 0;

void fiber_destroy(fiber_t* f)
{
    if(f) {
        assert(f->state == FIBER_STATE_DONE);
        fiber_context_destroy(&f->context);
        free(f);
    }
}

fiber_manager_t* fiber_manager_create(fiber_scheduler_t* scheduler)
{
    fiber_manager_t* const manager = calloc(1, sizeof(*manager));
    if(!manager) {
        errno = ENOMEM;
        return NULL;
    }

    manager->thread_fiber = fiber_create_from_thread();
    manager->current_fiber = manager->thread_fiber;
    manager->scheduler = scheduler;

    if(!manager->thread_fiber) {
        fiber_destroy(manager->thread_fiber);
        free(manager);
        return NULL;
    }
    return manager;
}

static void* fiber_manager_thread_func(void* param);

static inline void fiber_manager_switch_to(fiber_manager_t* manager, fiber_t* old_fiber, fiber_t* new_fiber)
{
    if(old_fiber->state == FIBER_STATE_RUNNING) {
        old_fiber->state = FIBER_STATE_READY;
        manager->to_schedule = old_fiber;
    }
    manager->current_fiber = new_fiber;
    manager->old_fiber = old_fiber;
    new_fiber->state = FIBER_STATE_RUNNING;
    fiber_context_swap(&old_fiber->context, &new_fiber->context);

    fiber_manager_do_maintenance();
}

void fiber_manager_yield(fiber_manager_t* manager)
{
    assert(fiber_manager_state == FIBER_MANAGER_STATE_STARTED);
    assert(manager);

    fiber_t* const current_fiber = manager->current_fiber;
    while(1) {
        manager->yield_count += 1;
        const fiber_state_t state = current_fiber->state;

        fiber_t* const new_fiber = fiber_scheduler_next(manager->scheduler);
        if(new_fiber) {
            fiber_manager_switch_to(manager, current_fiber, new_fiber);
            break;
        } else if(FIBER_STATE_WAITING == state
                  || FIBER_STATE_DONE == state
                  || FIBER_STATE_SAVING_STATE_TO_WAIT == state) {
            if(!manager->maintenance_fiber) {
                manager->maintenance_fiber = fiber_create_no_sched(102400, &fiber_manager_thread_func, manager);
                fiber_detach(manager->maintenance_fiber);
            }

            fiber_manager_switch_to(manager, current_fiber, manager->maintenance_fiber);
            //re-grab the manager, since we could be on a different thread now
            manager = fiber_manager_get();
        } else {
            //occasionally steal some work from threads with more load
            if((manager->yield_count & 1023) == 0) {
                fiber_scheduler_load_balance(manager->scheduler);
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

#ifdef USE_COMPILER_THREAD_LOCAL
__thread fiber_manager_t* fiber_the_manager = NULL;

fiber_manager_t* fiber_manager_get()
{
    return fiber_the_manager;
}
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

fiber_manager_t* fiber_manager_get()
{
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
}
#endif

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

    splitstack_disable_block_signals();

    fiber_manager_t* manager = (fiber_manager_t*)param;
    if(!manager->maintenance_fiber) {
        manager->maintenance_fiber = manager->thread_fiber;
    }

    while(!fiber_shutting_down) {
        fiber_scheduler_load_balance(manager->scheduler);

        fiber_t* const new_fiber = fiber_scheduler_next(manager->scheduler);
        if(new_fiber) {
            //make this fiber wait so we aren't scheduled again until all work is done
            manager->maintenance_fiber->state = FIBER_STATE_SAVING_STATE_TO_WAIT;
            fiber_manager_switch_to(manager, manager->maintenance_fiber, new_fiber);
        } else {
            const int num_events = fiber_poll_events();
            if(num_events == 0) {
                fiber_poll_events_blocking(0, FIBER_TIME_RESOLUTION_MS * 1000);
            }
        }
    }
    return NULL;
}

int fiber_manager_init(size_t num_threads)
{
    splitstack_disable_block_signals();

    if(fiber_manager_get_state() != FIBER_MANAGER_STATE_NONE) {
        errno = EINVAL;
        return FIBER_ERROR;
    }

    const int sched_ret = fiber_scheduler_init(num_threads);
    if(!sched_ret) {
        return FIBER_ERROR;
    }

    fiber_manager_threads = calloc(num_threads, sizeof(*fiber_manager_threads));
    assert(fiber_manager_threads);
    fiber_manager_num_threads = num_threads;
    fiber_managers = calloc(num_threads, sizeof(*fiber_managers));
    assert(fiber_managers);

    fiber_manager_t* const main_manager = fiber_manager_create(fiber_scheduler_for_thread(0));
    assert(main_manager);

#ifdef USE_COMPILER_THREAD_LOCAL
    fiber_the_manager = main_manager;
#else
    const int ret = pthread_setspecific(fiber_manager_key, main_manager);
    if(ret) {
        assert(0 && "pthread_setspecific() failed!");
        abort();
    }
#endif

    fiber_managers[0] = main_manager;

    fiber_manager_state = FIBER_MANAGER_STATE_STARTED;

    size_t i;
    for(i = 1; i < num_threads; ++i) {
        fiber_manager_t* const new_manager = fiber_manager_create(fiber_scheduler_for_thread(i));
        assert(new_manager);
        new_manager->id = i;
        fiber_managers[i] = new_manager;
    }

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 1024000);

    for(i = 1; i < num_threads; ++i) {
        if(pthread_create(&fiber_manager_threads[i], &attr, &fiber_manager_thread_func, fiber_managers[i])) {
            assert(0 && "failed to create kernel thread");
            fiber_manager_state = FIBER_MANAGER_STATE_ERROR;
            abort();
            return FIBER_ERROR;
        }
    }

    pthread_attr_destroy(&attr);

    if(!fiber_io_init()) {
        return FIBER_ERROR;
    }
    if(!fiber_event_init()) {
        return FIBER_ERROR;
    }

    return FIBER_SUCCESS;
}

void fiber_shutdown()
{
    fiber_shutting_down = 1;
    pthread_t self = pthread_self();
    int i;
    for(i = 0; i < fiber_manager_num_threads; ++i) {
        if(!pthread_equal(fiber_manager_threads[i], self)) {
            pthread_join(fiber_manager_threads[i], NULL);
        }
    }
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

extern mpmc_lifo_t fiber_free_fibers;

void fiber_manager_do_maintenance()
{
    fiber_manager_t* const manager = fiber_manager_get();

    fiber_t* const old_fiber = manager->old_fiber;
    if(old_fiber->state == FIBER_STATE_SAVING_STATE_TO_WAIT) {
        old_fiber->state = FIBER_STATE_WAITING;
    }

    if(manager->done_fiber) {
        mpsc_fifo_node_t* const node = manager->done_fiber->mpsc_fifo_node;
        node->data = manager->done_fiber;
        manager->done_fiber = NULL;
        mpmc_lifo_push(&fiber_free_fibers, node);
    }

    if(manager->to_schedule) {
        assert(manager->to_schedule->state == FIBER_STATE_READY);
        fiber_scheduler_schedule(manager->scheduler, manager->to_schedule);
        manager->to_schedule = NULL;
    }

    if(manager->mpmc_to_push.fifo) {
        mpmc_fifo_push(fiber_manager_get_hazard_record(manager), manager->mpmc_to_push.fifo, manager->mpmc_to_push.node);
        memset(&manager->mpmc_to_push, 0, sizeof(manager->mpmc_to_push));
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

void fiber_manager_wait_in_mpmc_queue(fiber_manager_t* manager, mpmc_fifo_t* fifo)
{
    assert(manager);
    assert(fifo);
    fiber_t* const this_fiber = manager->current_fiber;
    assert(this_fiber->state == FIBER_STATE_RUNNING);
    this_fiber->state = FIBER_STATE_WAITING;
    manager->mpmc_to_push.fifo = fifo;
    manager->mpmc_to_push.node = fiber_manager_get_mpmc_node();
    manager->mpmc_to_push.node->value = this_fiber;
    fiber_manager_yield(manager);
}

int fiber_manager_wake_from_mpmc_queue(fiber_manager_t* manager, mpmc_fifo_t* fifo, int count)
{
    //wake at least 'count' fibers; if count == 0, simply attempt to wake a fiber
    void* out = NULL;
    int wake_count = 0;
    hazard_pointer_thread_record_t* hptr = fiber_manager_get_hazard_record(manager);
    do {
        if((out = mpmc_fifo_trypop(hptr, fifo))) {
            count -= 1;
            fiber_t* const to_schedule = (fiber_t*)out;
            assert(to_schedule->state == FIBER_STATE_WAITING);
            to_schedule->state = FIBER_STATE_READY;
            fiber_manager_schedule(manager, to_schedule);
            wake_count += 1;
        } else if(count > 0) {
            cpu_relax();//back off if we failed to pop something
            manager->wake_mpmc_spin_count += 1;
        }
    } while(wake_count < count);
    return wake_count;
}

void fiber_manager_wait_in_mpsc_queue(fiber_manager_t* manager, mpsc_fifo_t* fifo)
{
    assert(manager);
    assert(fifo);
    fiber_t* const this_fiber = manager->current_fiber;
    assert(this_fiber->state == FIBER_STATE_RUNNING);
    assert(this_fiber->mpsc_fifo_node);
    this_fiber->state = FIBER_STATE_SAVING_STATE_TO_WAIT;
    mpsc_fifo_node_t* const node = this_fiber->mpsc_fifo_node;
    node->data = this_fiber;
    this_fiber->mpsc_fifo_node = NULL;
    mpsc_fifo_push(fifo, node);
    fiber_manager_yield(manager);
}

void fiber_manager_wait_in_mpsc_queue_and_unlock(fiber_manager_t* manager, mpsc_fifo_t* fifo, fiber_mutex_t* mutex)
{
    manager->mutex_to_unlock = mutex;
    fiber_manager_wait_in_mpsc_queue(manager, fifo);
}

int fiber_manager_wake_from_mpsc_queue(fiber_manager_t* manager, mpsc_fifo_t* fifo, int count)
{
    //wake at least 'count' fibers; if count == 0, simply attempt to wake a fiber
    mpsc_fifo_node_t* out = NULL;
    int wake_count = 0;
    do {
        if((out = mpsc_fifo_trypop(fifo))) {
            fiber_t* const to_schedule = (fiber_t*)out->data;
            assert(!to_schedule->mpsc_fifo_node);
            to_schedule->mpsc_fifo_node = out;
            if(to_schedule->state == FIBER_STATE_WAITING) {
                to_schedule->state = FIBER_STATE_READY;
            }
            fiber_manager_schedule(manager, to_schedule);
            wake_count += 1;
        } else if(count > 0) {
            manager->wake_mpsc_spin_count += 1;
            fiber_manager_yield(manager);
            manager = fiber_manager_get();
        }
    } while(wake_count < count);
    return wake_count;
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

typedef int (*usleepFnType)(useconds_t);
static usleepFnType fibershim_usleep = NULL;

void fiber_do_real_sleep(uint32_t seconds, uint32_t useconds)
{
    if(!fibershim_usleep) {
        fibershim_usleep = (usleepFnType)fiber_load_symbol("usleep");
    }
    while(seconds > 0) {
        fibershim_usleep(1000000);
        --seconds;
    }
    if(useconds) {
        fibershim_usleep(useconds);
    }
}

#define FIBER_MANAGER_MAX_HAZARDS MPMC_HAZARD_COUNT
static hazard_pointer_thread_record_t* fiber_hazard_head = NULL;

hazard_pointer_thread_record_t* fiber_manager_get_hazard_record(fiber_manager_t* manager)
{
    assert(manager);
    if(!manager->mpmc_hptr) {
        manager->mpmc_hptr = hazard_pointer_thread_record_create_and_push(&fiber_hazard_head, FIBER_MANAGER_MAX_HAZARDS);
    }
    return manager->mpmc_hptr;
}

static lockfree_ring_buffer_t* volatile fiber_free_mpmc_nodes = NULL;

static void fiber_manager_return_mpmc_node_internal(void* user_data, hazard_node_t* hazard)
{
    lockfree_ring_buffer_t* const free_nodes = fiber_free_mpmc_nodes;
    if(!free_nodes || !lockfree_ring_buffer_trypush(free_nodes, hazard)) {
        free(hazard);
    }
}

void fiber_manager_return_mpmc_node(mpmc_fifo_node_t* node)
{
    fiber_manager_return_mpmc_node_internal(NULL, &node->hazard);
}

mpmc_fifo_node_t* fiber_manager_get_mpmc_node()
{
    lockfree_ring_buffer_t* free_nodes = fiber_free_mpmc_nodes;
    if(!free_nodes) {
        free_nodes = lockfree_ring_buffer_create(10);
        if(!__sync_bool_compare_and_swap(&fiber_free_mpmc_nodes, NULL, free_nodes)) {
            lockfree_ring_buffer_destroy(free_nodes);
            free_nodes = fiber_free_mpmc_nodes;
        }
    }
    mpmc_fifo_node_t* ret = lockfree_ring_buffer_trypop(free_nodes);
    if(!ret) {
        ret = (mpmc_fifo_node_t*)malloc(sizeof(*ret));
        assert(ret);
        ret->hazard.gc_data = NULL;
        ret->hazard.gc_function = &fiber_manager_return_mpmc_node_internal;
    }
    return ret;
}

void fiber_manager_stats(fiber_manager_t* manager, fiber_manager_stats_t* out)
{
    assert(manager);
    assert(out);
    out->yield_count += manager->yield_count;
    fiber_scheduler_stats(manager->scheduler, &out->steal_count, &out->failed_steal_count);
    out->spin_count += manager->spin_count;
    out->signal_spin_count += manager->signal_spin_count;
    out->multi_signal_spin_count += manager->multi_signal_spin_count;
    out->wake_mpsc_spin_count += manager->wake_mpsc_spin_count;
    out->wake_mpmc_spin_count += manager->wake_mpmc_spin_count;
    out->poll_count += manager->poll_count;
    out->event_wait_count += manager->event_wait_count;
    out->lock_contention_count += manager->lock_contention_count;
}

void fiber_manager_all_stats(fiber_manager_stats_t* out)
{
    memset(out, 0, sizeof(*out));
    if(fiber_managers) {
        int i;
        for(i = 0; i < fiber_manager_num_threads; ++i) {
            fiber_manager_stats(fiber_managers[i], out);
        }
    }
}

