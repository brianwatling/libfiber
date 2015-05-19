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
#include <sys/resource.h>
#include <unistd.h>
#include <sys/poll.h>
#include <errno.h>
#include <stdio.h>
#if defined(LINUX)
#include <sys/timerfd.h>
#include <sys/epoll.h>
#elif defined(SOLARIS)
#include <port.h>
#include <signal.h>
#include <sys/time.h>
#else
#error OS not supported
#endif

typedef struct fd_wait_info
{
    int events;
    int added;
    fiber_spinlock_t spinlock;
    void* waiters;
} fd_wait_info_t;

static fd_wait_info_t* wait_info = NULL;
static int max_fd = 0;
static int event_fd = -1;
static fiber_spinlock_t sleep_spinlock = FIBER_SPINLOCK_INITIALIER;
static uint64_t timer_trigger_count = 0;

#if defined(LINUX)
static int timer_fd = -1;
typedef ssize_t (*readFnType) (int, void *, size_t);
static readFnType fibershim_read = NULL;
#elif defined(SOLARIS)
static timer_t timer_id = -1;
static hrtime_t last_timer_trigger = 0;
#else
#error OS not supported
#endif

//a tree of linked lists
typedef struct waiter_el
{
    uint64_t wake_time;
    void* waiter;
    struct waiter_el* next;
    struct waiter_el* left;
    struct waiter_el* right;
} waiter_el_t;

static waiter_el_t* sleepers = NULL;

void waiter_insert(waiter_el_t** tree, waiter_el_t* node)
{
    if(!(*tree)) {
        *tree = node;
        return;
    }

    while(1) {
        if(node->wake_time < (*tree)->wake_time) {
            tree = &(*tree)->left;
        } else if(node->wake_time == (*tree)->wake_time) {
            node->next = (*tree)->next;
            (*tree)->next = node;
            break;
        } else {
            tree = &(*tree)->right;
        }
        if(!(*tree)) {
            *tree = node;
            break;
        }
    }
}

waiter_el_t* waiter_remove_less_than(waiter_el_t** tree, const uint64_t wake_time)
{
    while(*tree) {
        if((*tree)->left) {
            tree = &(*tree)->left;
        } else if(wake_time > (*tree)->wake_time) {
            waiter_el_t* const ret = *tree;
            *tree = (*tree)->right;
            return ret;
        } else {
            return NULL;
        }
    }
    return NULL;
}

int fiber_event_init()
{
    if(event_fd >= 0) {
        return FIBER_ERROR;
    }

    struct rlimit file_lim;
    if(getrlimit(RLIMIT_NOFILE, &file_lim)) {
        return FIBER_ERROR;
    }
    max_fd = file_lim.rlim_max;

    wait_info = calloc(max_fd, sizeof(*wait_info));
    assert(wait_info);

#if defined(LINUX)
    timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    assert(timer_fd >= 0);
    struct itimerspec in = {};
    in.it_interval.tv_nsec = FIBER_TIME_RESOLUTION_MS * 1000000;//ms
    in.it_value.tv_nsec = FIBER_TIME_RESOLUTION_MS * 1000000;//ms
    struct itimerspec out;
    int ret = timerfd_settime(timer_fd, 0, &in, &out);
    assert(!ret);

    fibershim_read = (readFnType)fiber_load_symbol("read");

    const int the_event_fd = epoll_create(1);
    assert(the_event_fd >= 0);
    struct epoll_event e = {};
    e.events = EPOLLIN;
    e.data.fd = timer_fd;
    ret = epoll_ctl(the_event_fd, EPOLL_CTL_ADD, timer_fd, &e);
    assert(!ret);
#elif defined(SOLARIS)
    const int the_event_fd = port_create();
    assert(the_event_fd >= 0);

    port_notify_t notify_info = {};
    notify_info.portnfy_port = the_event_fd;

    struct sigevent evp;
    evp.sigev_notify = SIGEV_PORT;
    evp.sigev_value.sival_ptr = &notify_info;
    int ret = timer_create(CLOCK_REALTIME, &evp, &timer_id);
    assert(!ret);

    last_timer_trigger = gethrtime();

    itimerspec_t itimeout = {};
    itimeout.it_value.tv_sec = 0;
    itimeout.it_value.tv_nsec = FIBER_TIME_RESOLUTION_MS * 1000000;//ms
    itimeout.it_interval.tv_sec = 0;
    itimeout.it_interval.tv_nsec = FIBER_TIME_RESOLUTION_MS * 1000000;//ms
    ret = timer_settime(timer_id, 0, &itimeout, NULL);
    assert(!ret);
#else
#error OS not supported
#endif

    //set event_fd last, since it's used to check if the event system is initialized
    write_barrier();
    event_fd = the_event_fd;
    return FIBER_SUCCESS;
}

void fiber_event_destroy()
{
    if(event_fd < 0) {
        return;
    }

#if defined(LINUX)
    close(timer_fd);
    timer_fd = -1;
    close(event_fd);
    event_fd = -1;
#elif defined(SOLARIS)
    timer_delete(timer_id);
    timer_id = -1;
    close(event_fd);
    event_fd = -1;
#else
#error OS not supported
#endif

    free(wait_info);
    wait_info = NULL;
}

static void fiber_event_wake_waiters(fiber_manager_t* manager, fd_wait_info_t* info, intptr_t result)
{
    while(info->waiters) {
        fiber_t* const to_schedule = (fiber_t*)info->waiters;
        info->waiters = to_schedule->scratch;
        to_schedule->scratch = NULL;
        to_schedule->state = FIBER_STATE_READY;
        to_schedule->scratch = (void*)result;
        fiber_manager_schedule(manager, to_schedule);
    }
}

static void fiber_event_wake_sleepers(fiber_manager_t* manager, uint64_t trigger_count)
{
    fiber_spinlock_lock(&sleep_spinlock);
    timer_trigger_count += trigger_count;

    waiter_el_t* to_wake = NULL;
    while((to_wake = waiter_remove_less_than(&sleepers, timer_trigger_count))) {
        do {
            assert(to_wake->waiter);
            fiber_t* const to_schedule = (fiber_t*)to_wake->waiter;
            to_schedule->state = FIBER_STATE_READY;
            fiber_manager_schedule(manager, to_schedule);
            to_wake = to_wake->next;
        } while(to_wake);
    }

    fiber_spinlock_unlock(&sleep_spinlock);
}

static int fiber_poll_events_internal(uint32_t seconds, uint32_t useconds)
{
#if defined(LINUX)
    struct epoll_event events[64];
    const int count = epoll_wait(event_fd, events, 64, seconds * 1000 + useconds / 1000);
    if(count < 0) {
        if(errno == EINTR) { //interrupted, just try again later (could be gdb'ing etc)
            return 0;
        }
        assert(count >= 0 && "epoll_wait failed!");
        const char* err_msg = "epoll_wait failed!";
        const ssize_t ret = write(STDERR_FILENO, err_msg, strlen(err_msg));
        (void)ret;
        abort();
    }
    fiber_manager_t* const manager = fiber_manager_get();
    manager->poll_count += 1;
    int i;
    for(i = 0; i < count; ++i) {
        const int the_fd = events[i].data.fd;
        if(the_fd == timer_fd) {
            uint64_t timer_count = 0;
            const int ret = fibershim_read(timer_fd, &timer_count, sizeof(timer_count));
            if(ret != sizeof(timer_count)) {
                assert(errno == EWOULDBLOCK || errno == EAGAIN);
                continue;
            }
            fiber_event_wake_sleepers(manager, timer_count);
        } else {
            fd_wait_info_t* const info = &wait_info[the_fd];
            fiber_spinlock_lock(&info->spinlock);
            info->events &= ~events[i].events;
            info->events &= EPOLLIN | EPOLLOUT;
            if(info->events) {
                struct epoll_event e;
                e.events = EPOLLONESHOT | info->events;
                e.data.fd = the_fd;
                epoll_ctl(event_fd, EPOLL_CTL_MOD, e.data.fd, &e);
            }
            fiber_event_wake_waiters(manager, info, 0);
            fiber_spinlock_unlock(&info->spinlock);
        }
    }
    return count;
#elif defined(SOLARIS)
    port_event_t events[64];
    uint_t nget = 1;
    errno = 0;
    timespec_t timeout = {seconds, useconds * 1000};
    const int ret = port_getn(event_fd, events, 64, &nget, &timeout);
    fiber_manager_t* const manager = fiber_manager_get();
    manager->poll_count += 1;
    uint_t i;
    for(i = 0; i < nget; ++i) {
        port_event_t* const this_event = &events[i];
        if(this_event->portev_source == PORT_SOURCE_TIMER) {
            hrtime_t const now = gethrtime();
            const int timer_count = (now - last_timer_trigger) / (FIBER_TIME_RESOLUTION_MS * 1000000);
            last_timer_trigger += timer_count * (FIBER_TIME_RESOLUTION_MS * 1000000);
            fiber_event_wake_sleepers(manager, timer_count);
        } else if(this_event->portev_source == PORT_SOURCE_FD) {
            fd_wait_info_t* const info = &wait_info[this_event->portev_object];
            fiber_spinlock_lock(&info->spinlock);
            info->events &= ~this_event->portev_events;
            info->events &= POLLIN | POLLOUT;
            if(info->events) {
                port_associate(event_fd, PORT_SOURCE_FD, this_event->portev_object, info->events, NULL);
            }
            fiber_event_wake_waiters(manager, info, 0);
            fiber_spinlock_unlock(&info->spinlock);
        }
    }
    if(ret == -1 && errno != ETIME) {
        assert(0 && "port_getn failed!");
        const char* err_msg = "port_getn failed!";
        const ssize_t ret = write(STDERR_FILENO, err_msg, strlen(err_msg));
        (void)ret;
        abort();
    }
    return nget;
#else
#error OS not supported
#endif
}

int fiber_poll_events()
{
    if(event_fd < 0) {
        return FIBER_EVENT_NOTINIT;
    }

    return fiber_poll_events_internal(0, 0);
}

size_t fiber_poll_events_blocking(uint32_t seconds, uint32_t useconds)
{
    if(event_fd < 0) {
        fiber_do_real_sleep(seconds, useconds);
        return 0;
    }

    return fiber_poll_events_internal(seconds, useconds);
}

int fiber_wait_for_event(int fd, uint32_t events)
{
    assert(fd >= 0);
    assert(fd < max_fd);

    fd_wait_info_t* const info = &wait_info[fd];
    fiber_spinlock_lock(&info->spinlock);

#if defined(LINUX)
    if(events & FIBER_POLL_IN) {
        info->events |= EPOLLIN;
    }
    if(events & FIBER_POLL_OUT) {
        info->events |= EPOLLOUT;
    }
    struct epoll_event e = {};
    e.events = EPOLLONESHOT | info->events;
    e.data.fd = fd;

    if(!info->added) {
        epoll_ctl(event_fd, EPOLL_CTL_ADD, fd, &e);
        info->added = 1;
    } else {
        epoll_ctl(event_fd, EPOLL_CTL_MOD, fd, &e);
    }
#elif defined(SOLARIS)
    if(events & FIBER_POLL_IN) {
        info->events |= POLLIN;
    }
    if(events & FIBER_POLL_OUT) {
        info->events |= POLLOUT;
    }
    port_associate(event_fd, PORT_SOURCE_FD, fd, info->events, NULL);
#else
#error OS not supported
#endif

    fiber_manager_t* const manager = fiber_manager_get();
    manager->event_wait_count += 1;
    fiber_t* const this_fiber = manager->current_fiber;
    this_fiber->scratch = info->waiters;//use scratch field as a linked list of waiters
    info->waiters = this_fiber;
    this_fiber->state = FIBER_STATE_WAITING;
    manager->spinlock_to_unlock = &info->spinlock;
    fiber_manager_yield(manager);

    //if the fd is closed while we're polling, this_fiber->scratch will be non-NULL (see fiber_fd_closed)
    return this_fiber->scratch ? FIBER_ERROR : FIBER_SUCCESS;
}

int fiber_sleep(uint32_t seconds, uint32_t useconds)
{
    if(event_fd < 0) {
        fiber_do_real_sleep(seconds, useconds);
        return FIBER_SUCCESS;
    }

    const uint64_t sleep_ms = seconds * 1000 + useconds / 1000 + 1;//ms
    waiter_el_t wake_info = {};

    fiber_spinlock_lock(&sleep_spinlock);

    const uint64_t wake_time = timer_trigger_count + sleep_ms;
    wake_info.wake_time = wake_time;
    waiter_insert(&sleepers, &wake_info);

    fiber_manager_t* const manager = fiber_manager_get();
    fiber_t* const this_fiber = manager->current_fiber;
    wake_info.waiter = this_fiber;
    this_fiber->state = FIBER_STATE_WAITING;
    manager->spinlock_to_unlock = &sleep_spinlock;
    fiber_manager_yield(manager);

    return FIBER_SUCCESS;
}

void fiber_fd_closed(int fd)
{
    if(event_fd < 0) {
        return;
    }

    assert(fd >= 0);
    assert(fd < max_fd);
    fd_wait_info_t* const info = &wait_info[fd];
    fiber_spinlock_lock(&info->spinlock);
#if defined(LINUX)
    if(info->events || info->added) {
        epoll_ctl(event_fd, EPOLL_CTL_DEL, fd, NULL);
        info->events = 0;
        info->added = 0;
    }
#elif defined(SOLARIS)
    if(info->events) {
        port_dissociate(event_fd, PORT_SOURCE_FD, fd);
        info->events = 0;
    }
#else
#error OS not supported
#endif
    //setting result to -1 indicates to fiber_wait_for_event that the fd was closed
    fiber_event_wake_waiters(fiber_manager_get(), info, -1);
    fiber_spinlock_unlock(&info->spinlock);
}

