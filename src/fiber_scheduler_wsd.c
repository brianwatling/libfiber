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

#include "fiber_scheduler.h"
#include "work_stealing_deque.h"
#include <assert.h>
#include <stddef.h>

typedef struct fiber_scheduler_wsd
{
    wsd_work_stealing_deque_t* queue_one;
    wsd_work_stealing_deque_t* queue_two;
    wsd_work_stealing_deque_t* volatile schedule_from;
    wsd_work_stealing_deque_t* volatile store_to;
    size_t id;
    uint64_t steal_count;
    uint64_t failed_steal_count;
} fiber_scheduler_wsd_t;

static size_t fiber_scheduler_num_threads = 0;
static fiber_scheduler_wsd_t* fiber_schedulers = NULL;
static wsd_work_stealing_deque_t** fiber_scheduler_thread_queues = NULL;

int fiber_scheduler_wsd_init(fiber_scheduler_wsd_t* scheduler, size_t id)
{
    assert(scheduler);
    scheduler->queue_one = wsd_work_stealing_deque_create();
    scheduler->queue_two = wsd_work_stealing_deque_create();
    scheduler->schedule_from = scheduler->queue_one;
    scheduler->store_to = scheduler->queue_two;
    scheduler->id = id;
    scheduler->steal_count = 0;
    scheduler->failed_steal_count = 0;

    if(!scheduler->queue_one || !scheduler->queue_two) {
        wsd_work_stealing_deque_destroy(scheduler->queue_one);
        wsd_work_stealing_deque_destroy(scheduler->queue_two);
        return 0;
    }
    return 1;
}

int fiber_scheduler_init(size_t num_threads)
{
    assert(num_threads > 0);
    fiber_scheduler_num_threads = num_threads;

    fiber_schedulers = calloc(num_threads, sizeof(*fiber_schedulers));
    assert(fiber_schedulers);
    fiber_scheduler_thread_queues = calloc(2 * num_threads, sizeof(*fiber_scheduler_thread_queues));
    assert(fiber_scheduler_thread_queues);

    size_t i;
    for(i = 0; i < num_threads; ++i) {
        const int ret = fiber_scheduler_wsd_init(&fiber_schedulers[i], i);
        (void)ret;
        assert(ret);
        fiber_scheduler_thread_queues[i * 2] = fiber_schedulers[i].queue_one;
        fiber_scheduler_thread_queues[i * 2 + 1] = fiber_schedulers[i].queue_two;
    }
    return 1;
}

fiber_scheduler_t* fiber_scheduler_for_thread(size_t thread_id)
{
    assert(fiber_schedulers);
    assert(thread_id < fiber_scheduler_num_threads);
    return (fiber_scheduler_t*)&fiber_schedulers[thread_id];
}

void fiber_scheduler_schedule(fiber_scheduler_t* scheduler, fiber_t* the_fiber)
{
    assert(scheduler);
    assert(the_fiber);
    wsd_work_stealing_deque_push_bottom(((fiber_scheduler_wsd_t*)scheduler)->schedule_from, the_fiber);
}

fiber_t* fiber_scheduler_next(fiber_scheduler_t* sched)
{
    fiber_scheduler_wsd_t* const scheduler = (fiber_scheduler_wsd_t*)sched;
    assert(scheduler);
    if(wsd_work_stealing_deque_size(scheduler->schedule_from) == 0) {
        wsd_work_stealing_deque_t* const temp = scheduler->schedule_from;
        scheduler->schedule_from = scheduler->store_to;
        scheduler->store_to = temp;
    }

    while(wsd_work_stealing_deque_size(scheduler->schedule_from) > 0) {
        fiber_t* const new_fiber = (fiber_t*)wsd_work_stealing_deque_pop_bottom(scheduler->schedule_from);
        if(new_fiber != WSD_EMPTY && new_fiber != WSD_ABORT) {
            if(new_fiber->state == FIBER_STATE_SAVING_STATE_TO_WAIT) {
                wsd_work_stealing_deque_push_bottom(scheduler->store_to, new_fiber);
            } else {
                return new_fiber;
            }
        }
    }
    return NULL;
}

void fiber_scheduler_load_balance(fiber_scheduler_t* sched)
{
    fiber_scheduler_wsd_t* const scheduler = (fiber_scheduler_wsd_t*)sched;
    size_t max_steal = 50;
    size_t i = 2 * (scheduler->id + 1);
    const size_t end = i + 2 * (fiber_scheduler_num_threads - 1);
    const size_t mod = 2 * fiber_scheduler_num_threads;
    size_t local_count = wsd_work_stealing_deque_size(scheduler->schedule_from);
    for(; i < end; ++i) {
        const size_t index = i % mod;
        wsd_work_stealing_deque_t* const remote_queue = fiber_scheduler_thread_queues[index];
        assert(remote_queue != scheduler->queue_one);
        assert(remote_queue != scheduler->queue_two);
        if(!remote_queue) {
            continue;
        }
        size_t remote_count = wsd_work_stealing_deque_size(remote_queue);
        while(remote_count > local_count && max_steal > 0) {
            fiber_t* const stolen = (fiber_t*)wsd_work_stealing_deque_steal(remote_queue);
            if(stolen == WSD_EMPTY || stolen == WSD_ABORT) {
                ++scheduler->failed_steal_count;
                break;
            }
            wsd_work_stealing_deque_push_bottom(scheduler->schedule_from, stolen);
            --remote_count;
            ++local_count;
            --max_steal;
            ++scheduler->steal_count;
        }
    }
}

void fiber_scheduler_stats(fiber_scheduler_t* sched, uint64_t* steal_count, uint64_t* failed_steal_count)
{
    fiber_scheduler_wsd_t* const scheduler = (fiber_scheduler_wsd_t*)sched;
    assert(scheduler);
    *steal_count += scheduler->steal_count;
    *failed_steal_count += scheduler->failed_steal_count;
}

