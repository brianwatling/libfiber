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
#include "fiber_mutex.h"
#include "fiber_manager.h"
#include "mpsc_fifo.h"

typedef struct fiber_channel
{
    fiber_mutex_t mutex;
    mpsc_fifo_t send_waiters;
    mpsc_fifo_t recv_waiters;
    size_t size;
    volatile ssize_t balance;
    volatile size_t low;
    volatile size_t high;
    void* messages[];
} fiber_channel_t;

static inline fiber_channel_t* fiber_channel_create(size_t buffer_size)
{
    assert(buffer_size);
    const size_t required_size = sizeof(fiber_channel_t) + buffer_size * sizeof(void*);
    fiber_channel_t* const channel = (fiber_channel_t*)calloc(1, required_size);
    if(channel) {
        channel->size = buffer_size;
        if(!fiber_mutex_init(&channel->mutex)
           || !mpsc_fifo_init(&channel->send_waiters)
           || !mpsc_fifo_init(&channel->recv_waiters)) {
            fiber_mutex_destroy(&channel->mutex);//safe to call on uninitialied fifo
            mpsc_fifo_destroy(&channel->send_waiters);//safe to call on uninitialied fifo
            mpsc_fifo_destroy(&channel->recv_waiters);//safe to call on uninitialied fifo
            free(channel);
            return NULL;
        }
    }
    return channel;
}

static inline void fiber_channel_destroy(fiber_channel_t* channel)
{
    fiber_mutex_destroy(&channel->mutex);
    mpsc_fifo_destroy(&channel->send_waiters);
    mpsc_fifo_destroy(&channel->recv_waiters);
    free(channel);
}

static inline void fiber_channel_send(fiber_channel_t* channel, void* message)
{
    assert(channel);
    fiber_mutex_lock(&channel->mutex);

    ++channel->balance;

    //block if the channel is full
    while(channel->high - channel->low >= channel->size) {
        fiber_manager_wait_in_mpsc_queue_and_unlock(fiber_manager_get(), &channel->send_waiters, &channel->mutex);
        fiber_mutex_lock(&channel->mutex);
    }

    //insert the message
    const size_t high = channel->high;
    channel->messages[high % channel->size] = message;
    const size_t new_high = high + 1;
    channel->high = new_high;

    //wake a receiver if needed
    if(new_high - channel->low > 0) {
        fiber_manager_wake_from_mpsc_queue(fiber_manager_get(), &channel->recv_waiters, 0);
    }

    fiber_mutex_unlock(&channel->mutex);
}

static inline void* fiber_channel_receive(fiber_channel_t* channel)
{
    assert(channel);
    fiber_mutex_lock(&channel->mutex);

    --channel->balance;

    //block if the channel is empty
    while(channel->high - channel->low == 0) {
        fiber_manager_wait_in_mpsc_queue_and_unlock(fiber_manager_get(), &channel->recv_waiters, &channel->mutex);
        fiber_mutex_lock(&channel->mutex);
    }
    assert(channel->high - channel->low > 0);

    //read the message
    const size_t low = channel->low;
    void* const message = channel->messages[low % channel->size];
    const size_t new_low = low + 1;
    channel->low = new_low;

    //wake a sender if needed
    if(channel->high - new_low < channel->size) {
        fiber_manager_wake_from_mpsc_queue(fiber_manager_get(), &channel->send_waiters, 0);
    }

    fiber_mutex_unlock(&channel->mutex);
    return message;
}

#endif

