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

//a multi-sender single-receiver channel with a limit on the number of outstanding messages
typedef struct fiber_bounded_channel
{
    //putting high and low on separate cache lines provides a slight performance increase
    volatile uint64_t high;
    char _cache_padding1[CACHE_SIZE - sizeof(uint64_t)];
    volatile uint64_t low;
    char _cache_padding2[CACHE_SIZE - sizeof(uint64_t)];
    mpsc_fifo_t waiters;
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
            channel->buffer[index] = message;//TODO: wake reader?
            break;
        }
        fiber_yield();//TODO: go to sleep?
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
        if(ret
           && high > low
           && __sync_bool_compare_and_swap(&channel->low, low, low + 1)) {
            channel->buffer[index] = 0;
            return ret;
        }
        fiber_yield();//TODO: wake senders? go to sleep?
    }
    return NULL;
}

#endif

