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
#include "dist_fifo.h"
#include <assert.h>
#include <stddef.h>

typedef struct fiber_scheduler_dist
{
    dist_fifo_t queue;
    size_t id;
    uint64_t steal_count;
    uint64_t failed_steal_count;
} fiber_scheduler_dist_t;

static size_t fiber_scheduler_num_threads = 0;
static fiber_scheduler_dist_t* fiber_schedulers = NULL;

int fiber_scheduler_dist_init(fiber_scheduler_dist_t* scheduler, size_t id)
{
    assert(scheduler);
    scheduler->id = id;
    scheduler->steal_count = 0;
    scheduler->failed_steal_count = 0;
    if(!dist_fifo_init(&scheduler->queue)) {
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

    size_t i;
    for(i = 0; i < num_threads; ++i) {
        const int ret = fiber_scheduler_dist_init(&fiber_schedulers[i], i);
        (void)ret;
        assert(ret);
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
    mpsc_fifo_node_t* const node = the_fiber->mpsc_fifo_node;
    assert(node);
    the_fiber->mpsc_fifo_node = NULL;
    node->data = the_fiber;
    dist_fifo_push(&((fiber_scheduler_dist_t*)scheduler)->queue, node);
}

fiber_t* fiber_scheduler_next(fiber_scheduler_t* sched)
{
    fiber_scheduler_dist_t* const scheduler = (fiber_scheduler_dist_t*)sched;
    assert(scheduler);
    dist_fifo_node_t* node = NULL;
    while(1) {
        do {
            node = dist_fifo_trypop(&scheduler->queue);
        } while(node == DIST_FIFO_RETRY);
        if(!node) {
            break;
        }
        fiber_t* const new_fiber = (fiber_t*)node->data;
        if(new_fiber->state == FIBER_STATE_SAVING_STATE_TO_WAIT) {
            dist_fifo_push(&scheduler->queue, node);
        } else {
            new_fiber->mpsc_fifo_node = node;
            return new_fiber;
        }
    }
    return NULL;
}

void fiber_scheduler_load_balance(fiber_scheduler_t* sched)
{
    fiber_scheduler_dist_t* const scheduler = (fiber_scheduler_dist_t*)sched;
    size_t max_steal = 16;
    size_t i = scheduler->id + 1;
    const size_t end = i + fiber_scheduler_num_threads - 1;
    const size_t mod = fiber_scheduler_num_threads;
    for(; i < end; ++i) {
        const size_t index = i % mod;
        dist_fifo_t* const remote_queue = &fiber_schedulers[index].queue;
        assert(remote_queue != &scheduler->queue);
        if(!remote_queue) {
            continue;
        }
        while(max_steal > 0) {
            dist_fifo_node_t* const stolen = dist_fifo_trypop(remote_queue);
            if(stolen == DIST_FIFO_EMPTY || stolen == DIST_FIFO_RETRY) {
                ++scheduler->failed_steal_count;
                break;
            }
            dist_fifo_push(&scheduler->queue, stolen);
            --max_steal;
            ++scheduler->steal_count;
        }
    }
}

void fiber_scheduler_stats(fiber_scheduler_t* sched, uint64_t* steal_count, uint64_t* failed_steal_count)
{
    fiber_scheduler_dist_t* const scheduler = (fiber_scheduler_dist_t*)sched;
    assert(scheduler);
    *steal_count += scheduler->steal_count;
    *failed_steal_count += scheduler->failed_steal_count;
}






















