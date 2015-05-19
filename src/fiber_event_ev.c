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

#include "fiber_event.h"
#include "fiber.h"
#include "fiber_manager.h"
#include "fiber_spinlock.h"
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#ifndef __USE_GNU
#define __USE_GNU
#endif
#include <dlfcn.h>
#include <stdio.h>
#define EV_STANDALONE 1
#include <ev.h>

static struct ev_loop* volatile fiber_loop = NULL;
static fiber_spinlock_t fiber_loop_spinlock = FIBER_SPINLOCK_INITIALIER;
static volatile int num_events_triggered = 0;
static volatile int active_threads = 0;

int fiber_event_init()
{
    fiber_spinlock_lock(&fiber_loop_spinlock);

    assert("libev version mismatch" && ev_version_major () == EV_VERSION_MAJOR && ev_version_minor () >= EV_VERSION_MINOR);

    active_threads = fiber_manager_get_kernel_thread_count();

    write_barrier();//needed so active_threads is set before fiber_loop (see fiber_poll_events_blocking - active_threads should never be decremented before it's been set)

    fiber_loop = ev_loop_new(EVFLAG_AUTO);
    assert(fiber_loop);

    fiber_spinlock_unlock(&fiber_loop_spinlock);

    return fiber_loop ? FIBER_SUCCESS : FIBER_ERROR;
}

void fiber_event_destroy()
{
    fiber_spinlock_lock(&fiber_loop_spinlock);
    if(fiber_loop) {
        ev_loop_destroy(fiber_loop);
        fiber_loop = NULL;
    }
    fiber_spinlock_unlock(&fiber_loop_spinlock);
}

int fiber_poll_events()
{
    if(!fiber_loop) {
        return FIBER_EVENT_NOTINIT;
    }

    if(!fiber_spinlock_trylock(&fiber_loop_spinlock)) {
        return FIBER_EVENT_TRYAGAIN;
    }

    if(!fiber_loop) {
        fiber_spinlock_unlock(&fiber_loop_spinlock);
        return FIBER_EVENT_NOTINIT;
    }

    fiber_manager_get()->poll_count += 1;
    num_events_triggered = 0;
    ev_run(fiber_loop, EVRUN_NOWAIT);
    const int local_copy = num_events_triggered;
    fiber_spinlock_unlock(&fiber_loop_spinlock);

    return local_copy;
}

size_t fiber_poll_events_blocking(uint32_t seconds, uint32_t useconds)
{
    if(!fiber_loop) {
        fiber_do_real_sleep(seconds, useconds);
        return 0;
    }

    load_load_barrier();//needed to ensure we read fiber_loop first - we need a valid active_thread count,
                        //so init writes active_threads then fiber_loop, we read here in the reverse order

    //only allow the final thread to perform a blocking poll - this prevents the
    //thread from locking out other threads trying to register for events
    const int local_count = __sync_sub_and_fetch(&active_threads, 1);
    assert(local_count >= 0);
    if(local_count > 0) {
        fiber_do_real_sleep(seconds, useconds);
        __sync_add_and_fetch(&active_threads, 1);
        return 0;
    }

    fiber_spinlock_lock(&fiber_loop_spinlock);

    if(!fiber_loop) {
        __sync_add_and_fetch(&active_threads, 1);
        fiber_spinlock_unlock(&fiber_loop_spinlock);
        return 0;
    }

    num_events_triggered = 0;
    fiber_manager_get()->poll_count += 1;
    ev_run(fiber_loop, EVRUN_ONCE);
    const int local_copy = num_events_triggered;
    fiber_spinlock_unlock(&fiber_loop_spinlock);

    __sync_add_and_fetch(&active_threads, 1);
    return local_copy;
}

static void fd_ready(struct ev_loop* loop, ev_io* watcher, int revents)
{
    ev_io_stop(loop, watcher);
    fiber_manager_t* const manager = fiber_manager_get();
    fiber_t* const the_fiber = watcher->data;
    the_fiber->state = FIBER_STATE_READY;
    fiber_manager_schedule(manager, the_fiber);
    ++num_events_triggered;
}

int fiber_wait_for_event(int fd, uint32_t events)
{
    ev_io fd_event = {};
    int poll_events = 0;
    if(events & FIBER_POLL_IN) {
        poll_events |= EV_READ;
    }
    if(events & FIBER_POLL_OUT) {
        poll_events |= EV_WRITE;
    }
    //this code should really use ev_io_init(), but ev_io_init has compile warnings.
    ev_set_cb(&fd_event, &fd_ready);
    ev_io_set(&fd_event, fd, poll_events);

    fiber_spinlock_lock(&fiber_loop_spinlock);

    fiber_manager_t* const manager = fiber_manager_get();
    manager->event_wait_count += 1;
    fiber_t* const this_fiber = manager->current_fiber;

    fd_event.data = this_fiber;
    ev_io_start(fiber_loop, &fd_event);

    this_fiber->state = FIBER_STATE_WAITING;
    manager->spinlock_to_unlock = &fiber_loop_spinlock;

    fiber_manager_yield(manager);

    return FIBER_SUCCESS;
}

static void timer_trigger(struct ev_loop* loop, ev_timer* watcher, int revents)
{
    ev_timer_stop(loop, watcher);
    fiber_manager_t* const manager = fiber_manager_get();
    fiber_t* const the_fiber = watcher->data;
    the_fiber->state = FIBER_STATE_READY;
    fiber_manager_schedule(manager, the_fiber);
    ++num_events_triggered;
}

int fiber_sleep(uint32_t seconds, uint32_t useconds)
{
    if(!fiber_loop) {
        fiber_do_real_sleep(seconds, useconds);
        return FIBER_SUCCESS;
    }

    //this code should really use ev_timer_init(), but ev_timer_init has compile warnings.
    ev_timer timer_event = {};
    ev_set_cb(&timer_event, &timer_trigger);
    const double sleep_time = seconds + useconds * 0.000001;
    timer_event.at = sleep_time;
    timer_event.repeat = 0;

    fiber_spinlock_lock(&fiber_loop_spinlock);

    fiber_manager_t* const manager = fiber_manager_get();
    fiber_t* const this_fiber = manager->current_fiber;

    timer_event.data = this_fiber;

    ev_timer_start(fiber_loop, &timer_event);

    this_fiber->state = FIBER_STATE_WAITING;
    manager->spinlock_to_unlock = &fiber_loop_spinlock;

    fiber_manager_yield(manager);

    return FIBER_SUCCESS;
}

void fiber_fd_closed(int fd)
{
    //NOP
}

