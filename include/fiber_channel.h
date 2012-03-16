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
#include "fiber_signal.h"

//a multi-sender single-receiver channel with a limit on the number of outstanding messages. careful: senders and receivers spin!
typedef struct fiber_bounded_channel
{
    //putting high and low on separate cache lines provides a slight performance increase
    volatile uint64_t high;
    char _cache_padding1[CACHE_SIZE - sizeof(uint64_t)];
    volatile uint64_t low;
    char _cache_padding2[CACHE_SIZE - sizeof(uint64_t)];
    size_t size;
    void* buffer[];
} fiber_bounded_channel_t;

static inline fiber_bounded_channel_t* fiber_bounded_channel_create(size_t size)
{
    assert(size);
    const size_t required_size = sizeof(fiber_bounded_channel_t) + size * sizeof(void*);
    fiber_bounded_channel_t* const channel = (fiber_bounded_channel_t*)calloc(1, required_size);
    if(channel) {
        channel->size = size;
    }
    return channel;
}

static inline void fiber_bounded_channel_destroy(fiber_bounded_channel_t* channel)
{
    if(channel) {
        free(channel);
    }
}

static inline void fiber_bounded_channel_send(fiber_bounded_channel_t* channel, void* message)
{
    assert(channel);
    assert(message);//can't store NULLs; we rely on a NULL to indicate a spot in the buffer has not been written yet

    while(1) {
        const uint64_t low = channel->low;
        load_load_barrier();//read low first; this means the buffer will appear larger or equal to its actual size
        const uint64_t high = channel->high;
        const uint64_t index = high % channel->size;
        if(!channel->buffer[index]
           && high - low < channel->size
           && __sync_bool_compare_and_swap(&channel->high, high, high + 1)) {
            channel->buffer[index] = message;
            break;
        }
        fiber_yield();
    }
}

static inline void* fiber_bounded_channel_receive(fiber_bounded_channel_t* channel)
{
    assert(channel);

    while(1) {
        const uint64_t high = channel->high;
        load_load_barrier();//read high first; this means the buffer will appear smaller or equal to its actual size
        const uint64_t low = channel->low;
        const uint64_t index = low % channel->size;
        void* const ret = channel->buffer[index];
        if(ret && high > low) {
            channel->buffer[index] = 0;
            write_barrier();
            channel->low = low + 1;
            return ret;
        }
        fiber_yield();
    }
    return NULL;
}

//same as fiber_bounded_channel except send and receive will block
typedef struct fiber_blocking_bounded_channel
{
    //putting high and low on separate cache lines provides a slight performance increase
    volatile uint64_t high;
    char _cache_padding1[CACHE_SIZE - sizeof(uint64_t)];
    volatile uint64_t low;
    char _cache_padding2[CACHE_SIZE - sizeof(uint64_t)];
    volatile uint64_t send_count;
    mpsc_fifo_t waiters;
    fiber_signal_t ready_signal;
    size_t size;
    void* buffer[];
} fiber_blocking_bounded_channel_t;

static inline fiber_blocking_bounded_channel_t* fiber_blocking_bounded_channel_create(size_t size)
{
    assert(size);
    const size_t required_size = sizeof(fiber_blocking_bounded_channel_t) + size * sizeof(void*);
    fiber_blocking_bounded_channel_t* const channel = (fiber_blocking_bounded_channel_t*)calloc(1, required_size);
    if(channel) {
        channel->size = size;
        channel->send_count = 0;
        fiber_signal_init(&channel->ready_signal);
        if(!mpsc_fifo_init(&channel->waiters)) {
            free(channel);
            return 0;
        }
    }
    return channel;
}

static inline void fiber_blocking_bounded_channel_destroy(fiber_blocking_bounded_channel_t* channel)
{
    if(channel) {
        mpsc_fifo_destroy(&channel->waiters);
        free(channel);
    }
}

//returns 1 if a fiber was scheduled
static inline int fiber_blocking_bounded_channel_send(fiber_blocking_bounded_channel_t* channel, void* message)
{
    assert(channel);
    assert(message);//can't store NULLs; we rely on a NULL to indicate a spot in the buffer has not been written yet

    __sync_fetch_and_add(&channel->send_count, 1);

    while(1) {
        const uint64_t low = channel->low;
        load_load_barrier();//read low first; this means the buffer will appear larger or equal to its actual size
        const uint64_t high = channel->high;
        const uint64_t index = high % channel->size;
        if(!channel->buffer[index]
           && high - low < channel->size
           && __sync_bool_compare_and_swap(&channel->high, high, high + 1)) {
            channel->buffer[index] = message;
            return fiber_signal_raise(&channel->ready_signal);
        }
        fiber_manager_wait_in_mpsc_queue(fiber_manager_get(), &channel->waiters);
    }
    return 0;
}

static inline void* fiber_blocking_bounded_channel_receive(fiber_blocking_bounded_channel_t* channel)
{
    assert(channel);

    while(1) {
        const uint64_t send_count = channel->send_count;
        load_load_barrier();
        const uint64_t high = channel->high;
        load_load_barrier();//read high first; this means the buffer will appear smaller or equal to its actual size
        const uint64_t low = channel->low;
        const uint64_t index = low % channel->size;
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
        fiber_signal_wait(&channel->ready_signal);
    }
    return NULL;
}

//mulitple senders are wait free, single receiver will block
typedef struct fiber_unbounded_channel
{
    mpsc_fifo_t queue;
    fiber_signal_t ready_signal;
} fiber_unbounded_channel_t;

typedef mpsc_fifo_node_t fiber_unbounded_channel_message_t;

static inline int fiber_unbounded_channel_init(fiber_unbounded_channel_t* channel)
{
    assert(channel);
    fiber_signal_init(&channel->ready_signal);
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
    return fiber_signal_raise(&channel->ready_signal);
}

//the caller owns the message when this function returns
static inline void* fiber_unbounded_channel_receive(fiber_unbounded_channel_t* channel)
{
    assert(channel);

    fiber_unbounded_channel_message_t* ret;
    while(!(ret = mpsc_fifo_trypop(&channel->queue))) {
        fiber_signal_wait(&channel->ready_signal);
    }
    return ret;
}

#endif

