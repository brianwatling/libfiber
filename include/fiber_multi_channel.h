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
#include "lockfree_ring_buffer.h"
#include "fiber_mutex.h"
#include "fiber_signal.h"

//a bounded channel with many senders and receivers. send and receive will block if necessary.
typedef struct fiber_multi_channel
{
    fiber_mutex_t writer_signal_mutex;
    fiber_signal_t writer_signal;
    fiber_mutex_t reader_signal_mutex;
    fiber_signal_t reader_signal;
    volatile uint64_t high;
    volatile uint64_t low;
    void* sentinel;
    uint32_t size;
    //buffer must be last - it spills outside of this struct
    void* buffer[];
} fiber_multi_channel_t;

static inline fiber_multi_channel_t* fiber_multi_channel_create(uint32_t size, void* sentinel)
{
    assert(size);
    const size_t required_size = sizeof(fiber_multi_channel_t) + size * sizeof(void*);
    //NOTE: here we abuse the lockfree_ring_buffer by not initializing it via a function. this works because everything in it is zero except size
    fiber_multi_channel_t* const channel = (fiber_multi_channel_t*)calloc(1, required_size);
    if(channel) {
        channel->sentinel = sentinel;
        channel->size = size;
        fiber_signal_init(&channel->writer_signal);
        fiber_signal_init(&channel->reader_signal);
        if(!fiber_mutex_init(&channel->writer_signal_mutex)
           || !fiber_mutex_init(&channel->reader_signal_mutex)) {
            fiber_mutex_destroy(&channel->writer_signal_mutex);
            fiber_mutex_destroy(&channel->reader_signal_mutex);
            fiber_signal_destroy(&channel->writer_signal);
            fiber_signal_destroy(&channel->reader_signal);
            free(channel);
            return 0;
        }
        size_t i;
        void** buffer = channel->buffer;
        for(i = 0; i < size; ++i) {
            buffer[i] = sentinel;
        }
    }
    return channel;
}

static inline void fiber_multi_channel_destroy(fiber_multi_channel_t* channel)
{
    if(channel) {
        fiber_mutex_destroy(&channel->writer_signal_mutex);
        fiber_mutex_destroy(&channel->reader_signal_mutex);
        fiber_signal_destroy(&channel->writer_signal);
        fiber_signal_destroy(&channel->reader_signal);
        free(channel);
    }
}

typedef enum {
    FIBER_MULTI_CHANNEL_SUCCESS,
    FIBER_MULTI_CHANNEL_FAIL,
    FIBER_MULTI_CHANNEL_CONTENTION,
} fiber_multi_channel_result_t;

static inline fiber_multi_channel_result_t fiber_multi_channel_send_internal(fiber_multi_channel_t* channel, void* message)
{
    const size_t size = channel->size;
    const uint64_t low = channel->low;
    load_load_barrier();//read low first; this means the buffer will appear larger or equal to its actual size
    const uint64_t high = channel->high;
    const uint64_t index = high % size;
    void** const spot = &(channel->buffer[index]);
    if(*spot == channel->sentinel && (high - low < size)) {
        if(__sync_bool_compare_and_swap(&channel->high, high, high + 1)) {
            *spot = message;
            return FIBER_MULTI_CHANNEL_SUCCESS;
        }
        return FIBER_MULTI_CHANNEL_CONTENTION;
    }
    return FIBER_MULTI_CHANNEL_FAIL;
}

static inline int fiber_multi_channel_send(fiber_multi_channel_t* channel, void* message)
{
    assert(channel);
    if(message == channel->sentinel) {
        assert(message != channel->sentinel);
        *(int*)NULL = 0;//no. just no.
    }

    int locked = 0;
    while(1) {
        const fiber_multi_channel_result_t result = fiber_multi_channel_send_internal(channel, message);
        if(result == FIBER_MULTI_CHANNEL_SUCCESS) {
            break;
        } else if(result == FIBER_MULTI_CHANNEL_FAIL) {
            //the buffer was full - we may need to wait for a reader to receive something
            if(!locked) {
                //lock the signal and try again
                locked = 1;
                fiber_mutex_lock(&channel->writer_signal_mutex);
                continue;
            } else {
                //it's full and we own the signal - so wait for a signal
                fiber_signal_wait(&channel->writer_signal);
            }
        }
    }
    if(locked) {
        fiber_mutex_unlock(&channel->writer_signal_mutex);
    }
    //potentially wake up a blocked reader
    return fiber_signal_raise(&channel->reader_signal);
}

static inline fiber_multi_channel_result_t fiber_multi_channel_receive_internal(fiber_multi_channel_t* channel, void** result)
{
    void* const sentinel = channel->sentinel;
    const uint64_t high = channel->high;
    load_load_barrier();//read high first; this means the buffer will appear smaller or equal to its actual size
    const uint64_t low = channel->low;
    const uint64_t index = low % channel->size;
    void** const spot = &(channel->buffer[index]);
    void* const ret = *spot;
    if(ret != sentinel && high > low) {
        if(__sync_bool_compare_and_swap(&channel->low, low, low + 1)) {
            *spot = sentinel;
            *result = ret;
            return FIBER_MULTI_CHANNEL_SUCCESS;
        }
        return FIBER_MULTI_CHANNEL_CONTENTION;
    }
    return FIBER_MULTI_CHANNEL_FAIL;
}

static inline void* fiber_multi_channel_receive(fiber_multi_channel_t* channel)
{
    assert(channel);

    void* ret;
    int locked = 0;
    while(1) {
        fiber_multi_channel_result_t result = fiber_multi_channel_receive_internal(channel, &ret);
        if(result == FIBER_MULTI_CHANNEL_SUCCESS) {
            break;
        } else if(result == FIBER_MULTI_CHANNEL_FAIL) {
            //the buffer was empty - we may need to wait for a writer to send something
            if(!locked) {
                //lock the signal and try again
                locked = 1;
                fiber_mutex_lock(&channel->reader_signal_mutex);
                continue;
            } else {
                //it's full and we own the signal - so wait for a signal
                fiber_signal_wait(&channel->reader_signal);
            }
        }
    }
    fiber_signal_raise(&channel->writer_signal);
    return ret;
}

#endif

