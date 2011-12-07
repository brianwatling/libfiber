#include "fiber_event.h"
#include "fiber.h"
#include "fiber_manager.h"
#include "fiber_spinlock.h"
#include <sys/resource.h>
#include <unistd.h>
#include <sys/poll.h>
#if defined(LINUX)
#include <sys/epoll.h>
#elif defined(SOLARIS)
#include <port.h>
#else
#error OS not supported
#endif

typedef struct fd_wait_info
{
    int volatile events;
    fiber_spinlock_t spinlock;
    fiber_t* waiters;
} fd_wait_info_t;

static fd_wait_info_t* wait_info = NULL;
static int max_fd = 0;
static int event_fd = -1;

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

    write_barrier();

#if defined(LINUX)
    event_fd = epoll_create(1);
    assert(event_fd >= 0);
#elif defined(SOLARIS)
    event_fd = port_create();
    assert(event_fd >= 0);
#else
#error OS not supported
#endif
    return FIBER_SUCCESS;
}

void fiber_event_destroy()
{
    if(event_fd < 0) {
        return;
    }

#if defined(LINUX) || defined(SOLARIS)
    close(event_fd);
    event_fd = -1;
#else
#error OS not supported
#endif

    free(wait_info);
    wait_info = NULL;
}

static int fiber_poll_events_internal(int wait_ms)
{
    struct epoll_event events[64];
    const int count = epoll_wait(event_fd, events, 64, 0);
    assert(count >= 0);
    fiber_manager_t* const manager = fiber_manager_get();
    int i;
    for(i = 0; i < count; ++i) {
        fd_wait_info_t* const info = &wait_info[events[i].data.fd];
        fiber_spinlock_lock(&info->spinlock);
        info->events &= ~events[i].events;
        if(info->events) {
            struct epoll_event e;
            e.events = EPOLLONESHOT | info->events;
            e.data.fd = events[i].data.fd;
            epoll_ctl(event_fd, EPOLL_CTL_MOD, e.data.fd, &e);
        } else {
            epoll_ctl(event_fd, EPOLL_CTL_DEL, events[i].data.fd, NULL);
        }
        while(info->waiters) {
            fiber_t* const to_schedule = info->waiters;
            info->waiters = info->waiters->next;
            to_schedule->next = NULL;
            to_schedule->state = FIBER_STATE_READY;
            fiber_manager_schedule(manager, to_schedule);
        }
        fiber_spinlock_unlock(&info->spinlock);
    }
    return count;
}

int fiber_poll_events()
{
    if(event_fd < 0) {
        return FIBER_EVENT_NOTINIT;
    }

    return fiber_poll_events_internal(0);
}

size_t fiber_poll_events_blocking(uint32_t seconds, uint32_t useconds)
{
    if(event_fd < 0) {
        fiber_do_real_sleep(seconds, useconds);
        return 0;
    }

    return fiber_poll_events_internal(seconds * 1000 + useconds / 1000);
}

int fiber_wait_for_event(int fd, uint32_t events)
{
    assert(fd >= 0);
    assert(fd < max_fd);

    fd_wait_info_t* const info = &wait_info[fd];
    fiber_spinlock_lock(&info->spinlock);

#if defined(LINUX)
    int need_add = 0;
    if(!info->events) {
        need_add = 1;
    }

    if(events & FIBER_POLL_IN) {
        info->events |= EPOLLIN;
    }
    if(events & FIBER_POLL_OUT) {
        info->events |= EPOLLOUT;
    }
    struct epoll_event e;
    e.events = EPOLLONESHOT | info->events;
    e.data.fd = fd;

    if(need_add) {
        epoll_ctl(event_fd, EPOLL_CTL_ADD, fd, &e);
    } else {
        epoll_ctl(event_fd, EPOLL_CTL_MOD, fd, &e);
    }
#elif defined(SOLARIS)

#else
#error OS not supported
#endif

    fiber_manager_t* const manager = fiber_manager_get();
    fiber_t* const this_fiber = manager->current_fiber;
    this_fiber->next = info->waiters;
    info->waiters = this_fiber;
    this_fiber->state = FIBER_STATE_WAITING;
    manager->spinlock_to_unlock = &info->spinlock;
    fiber_manager_yield(manager);

    return FIBER_SUCCESS;
}

int fiber_sleep(uint32_t seconds, uint32_t useconds)
{
    assert(0 && "sleep not implemented!");
    return FIBER_SUCCESS;
}
