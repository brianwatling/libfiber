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

#ifndef _FIBER_CHANNEL_H_
#define _FIBER_CHANNEL_H_

/*
    Author: Brian Watling
    Email: brianwatling@hotmail.com
    Website: https://github.com/brianwatling
*/

#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <malloc.h>

#include "machine_specific.h"
#include "fiber_manager.h"
#include "fiber_mutex.h"
#include "fiber_signal.h"

//a bounded channel with many senders and receivers. send and receive will block if necessary.
typedef struct fiber_multi_channel
{
    fiber_mutex_t lock;
    uint64_t high;
    uint64_t low;
    uint32_t size;
    uint32_t power_of_2_mod;
    fiber_t* waiters;
    //buffer must be last - it spills outside of this struct
    void* buffer[];
} fiber_multi_channel_t;

static inline fiber_multi_channel_t* fiber_multi_channel_create(uint32_t power_of_2_size, void* sentinel)
{
    assert(power_of_2_size && power_of_2_size < 32);
    const size_t size = 1 << power_of_2_size;
    const size_t required_size = sizeof(fiber_multi_channel_t) + size * sizeof(void*);
    fiber_multi_channel_t* const channel = (fiber_multi_channel_t*)calloc(1, required_size);
    if(channel) {
        channel->size = size;
        channel->power_of_2_mod = size - 1;
        if(!fiber_mutex_init(&channel->lock)) {
            free(channel);
            return 0;
        }
    }
    return channel;
}

static inline void fiber_multi_channel_destroy(fiber_multi_channel_t* channel)
{
    if(channel) {
        fiber_mutex_destroy(&channel->lock);
        free(channel);
    }
}

static inline void fiber_multi_channel_internal_wait(fiber_multi_channel_t* channel)
{
    fiber_manager_t* const manager = fiber_manager_get();
    fiber_t* const this_fiber = manager->current_fiber;
    this_fiber->scratch = channel->waiters;
    channel->waiters = this_fiber;
    assert(this_fiber->state == FIBER_STATE_RUNNING);
    this_fiber->state = FIBER_STATE_WAITING;
    manager->mutex_to_unlock = &channel->lock;
    fiber_manager_yield(manager);
}

static inline void fiber_multi_channel_internal_wake(fiber_multi_channel_t* channel)
{
    if(channel->waiters) {
        fiber_t* const to_wake = channel->waiters;
        channel->waiters = to_wake->scratch;
        to_wake->scratch = NULL;
        to_wake->state = FIBER_STATE_READY;
        fiber_manager_schedule(fiber_manager_get(), to_wake);
    }
}

static inline void fiber_multi_channel_send(fiber_multi_channel_t* channel, void* message)
{
    assert(channel);

    while(1) {
        fiber_mutex_lock(&channel->lock);
        if(channel->high - channel->low < channel->size) {
            break;
        }
        fiber_multi_channel_internal_wait(channel);
    }
    const uint32_t index = channel->high & channel->power_of_2_mod;
    channel->buffer[index] = message;
    channel->high += 1;
    fiber_multi_channel_internal_wake(channel);
    fiber_mutex_unlock(&channel->lock);
}

static inline void* fiber_multi_channel_receive(fiber_multi_channel_t* channel)
{
    assert(channel);

    while(1) {
        fiber_mutex_lock(&channel->lock);
        if(channel->high > channel->low) {
            break;
        }
        fiber_multi_channel_internal_wait(channel);
    }
    const uint32_t index = channel->low & channel->power_of_2_mod;
    void* const ret = channel->buffer[index];
    channel->buffer[index] = 0;
    channel->low += 1;
    fiber_multi_channel_internal_wake(channel);
    fiber_mutex_unlock(&channel->lock);
    return ret;
}

#endif

