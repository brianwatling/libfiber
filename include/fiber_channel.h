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
#include "mpsc_fifo.h"
#include "spsc_fifo.h"
#include "fiber_signal.h"

//a bounded channel. send and receive will block. there can be many senders but only one receiver
typedef struct fiber_bounded_channel
{
    //putting high and low on separate cache lines provides a slight performance increase
    volatile uint64_t high;
    char _cache_padding1[CACHE_SIZE - sizeof(uint64_t)];
    volatile uint64_t low;
    char _cache_padding2[CACHE_SIZE - sizeof(uint64_t)];
    volatile uint64_t send_count;
    mpsc_fifo_t waiters;
    fiber_signal_t* ready_signal;
    uint32_t size;
    uint32_t power_of_2_mod;
    void* buffer[];
} fiber_bounded_channel_t;

//specifying a NULL signal means this channel will spin. 'signal' must out-live this channel
static inline fiber_bounded_channel_t* fiber_bounded_channel_create(uint32_t power_of_2_size, fiber_signal_t* signal)
{
    assert(power_of_2_size && power_of_2_size < 32);
    const uint32_t size = 1 << power_of_2_size;
    const uint32_t required_size = sizeof(fiber_bounded_channel_t) + size * sizeof(void*);
    fiber_bounded_channel_t* const channel = (fiber_bounded_channel_t*)calloc(1, required_size);
    if(channel) {
        channel->size = size;
        channel->power_of_2_mod = size - 1;
        channel->ready_signal = signal;
        if(!mpsc_fifo_init(&channel->waiters)) {
            free(channel);
            return 0;
        }
    }
    return channel;
}

static inline void fiber_bounded_channel_destroy(fiber_bounded_channel_t* channel)
{
    if(channel) {
        mpsc_fifo_destroy(&channel->waiters);
        free(channel);
    }
}

//returns 1 if a fiber was scheduled
static inline int fiber_bounded_channel_send(fiber_bounded_channel_t* channel, void* message)
{
    assert(channel);
    assert(message);//can't store NULLs; we rely on a NULL to indicate a spot in the buffer has not been written yet

    __sync_fetch_and_add(&channel->send_count, 1);

    while(1) {
        const uint64_t low = channel->low;
        load_load_barrier();//read low first; this means the buffer will appear larger or equal to its actual size
        const uint64_t high = channel->high;
        const uint64_t index = high & channel->power_of_2_mod;
        if(!channel->buffer[index]
           && high - low < channel->size
           && __sync_bool_compare_and_swap(&channel->high, high, high + 1)) {
            channel->buffer[index] = message;
            if(channel->ready_signal) {
                return fiber_signal_raise(channel->ready_signal);
            }
            return 0;
        }
        fiber_manager_wait_in_mpsc_queue(fiber_manager_get(), &channel->waiters);
    }
    return 0;
}

static inline void* fiber_bounded_channel_receive(fiber_bounded_channel_t* channel)
{
    assert(channel);

    while(1) {
        const uint64_t send_count = channel->send_count;
        load_load_barrier();
        const uint64_t high = channel->high;
        load_load_barrier();//read high first; this means the buffer will appear smaller or equal to its actual size
        const uint64_t low = channel->low;
        const uint64_t index = low & channel->power_of_2_mod;
        void* const ret = channel->buffer[index];
        if(ret && high > low) {
            channel->buffer[index] = 0;
            write_barrier();
            channel->low = low + 1;
            if(high < send_count) {
                fiber_manager_wake_from_mpsc_queue(fiber_manager_get(), &channel->waiters, 0);
            }
            return ret;
        }
        if(channel->ready_signal) {
            fiber_signal_wait(channel->ready_signal);
        }
    }
    return NULL;
}

static inline int fiber_bounded_channel_try_receive(fiber_bounded_channel_t* channel, void** out)
{
    assert(channel);

    const uint64_t send_count = channel->send_count;
    load_load_barrier();
    const uint64_t high = channel->high;
    load_load_barrier();//read high first; this means the buffer will appear smaller or equal to its actual size
    const uint64_t low = channel->low;
    const uint64_t index = low & channel->power_of_2_mod;
    void* const ret = channel->buffer[index];
    if(ret && high > low) {
        channel->buffer[index] = 0;
        write_barrier();
        channel->low = low + 1;
        if(high < send_count) {
            fiber_manager_wake_from_mpsc_queue(fiber_manager_get(), &channel->waiters, 0);
        }
        *out = ret;
        return 1;
    }
    return 0;
}

//a unbounded channel. send and receive will block. there can be many senders but only one receiver
typedef struct fiber_unbounded_channel
{
    mpsc_fifo_t queue;
    fiber_signal_t* ready_signal;
} fiber_unbounded_channel_t;

typedef mpsc_fifo_node_t fiber_unbounded_channel_message_t;

//specifying a NULL signal means this channel will spin. 'signal' must out-live this channel
static inline int fiber_unbounded_channel_init(fiber_unbounded_channel_t* channel, fiber_signal_t* signal)
{
    assert(channel);
    channel->ready_signal = signal;
    if(!mpsc_fifo_init(&channel->queue)) {
        return 0;
    }
    return 1;
}

static inline void fiber_unbounded_channel_destroy(fiber_unbounded_channel_t* channel)
{
    if(channel) {
        mpsc_fifo_destroy(&channel->queue);
    }
}

//the channel owns message when this function returns.
//returns 1 if a fiber was scheduled
static inline int fiber_unbounded_channel_send(fiber_unbounded_channel_t* channel, fiber_unbounded_channel_message_t* message)
{
    assert(channel);
    assert(message);

    mpsc_fifo_push(&channel->queue, message);
    if(channel->ready_signal) {
        return fiber_signal_raise(channel->ready_signal);
    }
    return 0;
}

//the caller owns the message when this function returns
static inline void* fiber_unbounded_channel_receive(fiber_unbounded_channel_t* channel)
{
    assert(channel);

    fiber_unbounded_channel_message_t* ret;
    while(!(ret = mpsc_fifo_trypop(&channel->queue))) {
        if(channel->ready_signal) {
            fiber_signal_wait(channel->ready_signal);
        }
    }
    return ret;
}

static inline void* fiber_unbounded_channel_try_receive(fiber_unbounded_channel_t* channel)
{
    assert(channel);
    return mpsc_fifo_trypop(&channel->queue);
}

//a unbounded channel. send and receive will block. there can be only one sender and one receiver
typedef struct fiber_unbounded_sp_channel
{
    spsc_fifo_t queue;
    fiber_signal_t* ready_signal;
} fiber_unbounded_sp_channel_t;

typedef spsc_node_t fiber_unbounded_sp_channel_message_t;

//specifying a NULL signal means this channel will spin. 'signal' must out-live this channel
static inline int fiber_unbounded_sp_channel_init(fiber_unbounded_sp_channel_t* channel, fiber_signal_t* signal)
{
    assert(channel);
    channel->ready_signal = signal;
    if(!spsc_fifo_init(&channel->queue)) {
        return 0;
    }
    return 1;
}

static inline void fiber_unbounded_sp_channel_destroy(fiber_unbounded_sp_channel_t* channel)
{
    if(channel) {
        spsc_fifo_destroy(&channel->queue);
    }
}

//the channel owns message when this function returns.
//returns 1 if a fiber was scheduled
static inline int fiber_unbounded_sp_channel_send(fiber_unbounded_sp_channel_t* channel, fiber_unbounded_sp_channel_message_t* message)
{
    assert(channel);
    assert(message);

    spsc_fifo_push(&channel->queue, message);
    if(channel->ready_signal) {
        return fiber_signal_raise(channel->ready_signal);
    }
    return 0;
}

//the caller owns the message when this function returns
static inline void* fiber_unbounded_sp_channel_receive(fiber_unbounded_sp_channel_t* channel)
{
    assert(channel);

    fiber_unbounded_sp_channel_message_t* ret;
    while(!(ret = spsc_fifo_trypop(&channel->queue))) {
        if(channel->ready_signal) {
            fiber_signal_wait(channel->ready_signal);
        }
    }
    return ret;
}

static inline void* fiber_unbounded_sp_channel_try_receive(fiber_unbounded_sp_channel_t* channel)
{
    assert(channel);
    return spsc_fifo_trypop(&channel->queue);
}

#endif

