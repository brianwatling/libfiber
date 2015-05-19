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

#ifndef _FIBER_FIBER_EVENT_H_
#define _FIBER_FIBER_EVENT_H_

#include <stddef.h>
#include <stdint.h>

//this variable controls how long idle threads wait for events, in milliseconds.
//the value is important: high values may be better for workloads which are not truly parallel,
//while lower values may allow idle threads to pick up new work sooner
//TODO: make this a runtime config option?
#define FIBER_TIME_RESOLUTION_MS 5 //ms

#ifdef __cplusplus
extern "C" {
#endif

extern int fiber_event_init();

extern void fiber_event_destroy();

#define FIBER_EVENT_NONE (0)
#define FIBER_EVENT_NOTINIT (-1)
#define FIBER_EVENT_TRYAGAIN (-2)

/* ABOUT EVENTS
When a fiber manager thread is out of fibers to schedule, it will poll for events by calling
fiber_poll_events(). if zero events are returned, it will enter a blocking poll by calling
fiber_poll_events_blocking()
*/

//called when a fiber manager thread is looking for events. returns the number of
//events triggered or NOTINIT/TRYAGAIN
extern int fiber_poll_events();

//called when a fiber manager thread is out of events and cannot steal any from other threads. the event system should
//perform a blocking poll. the implementation is allowed to sleep instead if it's not possible to
//register new events while performing a blocking poll. returns the number of events triggered.
extern size_t fiber_poll_events_blocking(uint32_t seconds, uint32_t useconds);

#define FIBER_POLL_IN (0x1)
#define FIBER_POLL_OUT (0x2)

//register to wait for an event. the calling fiber is suspended until the given fd is
//ready to perform the operation(s) specified by events
extern int fiber_wait_for_event(int fd, uint32_t events);

//puts the calling fiber to sleep
extern int fiber_sleep(uint32_t seconds, uint32_t useconds);

//called when a file descriptor is closed
extern void fiber_fd_closed(int fd);

#ifdef __cplusplus
}
#endif

#endif
